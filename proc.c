#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#include "uproc.h"


static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};

// debug routines
#ifdef DEBUG
static void checkProcs(const char * file, const char * function, int line);
#endif

#ifdef CS333_P3
struct ptrs {
    struct proc* head;
    struct proc* tail;
};

#define statecount NELEM(states)

// list management function prototypes
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void assertState(struct proc*, enum procstate, const char *, int);
#endif

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];

  #ifdef CS333_P3
  struct ptrs list[statecount];
  #endif
} ptable;

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  int found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }

  #ifdef CS333_P3
  stateListRemove(&ptable.list[UNUSED], p);
  assertState(p, UNUSED, __FUNCTION__, __LINE__);
  p->state = EMBRYO;
  stateListAdd(&ptable.list[EMBRYO], p);
  #endif // CS333_P3

  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    acquire(&ptable.lock);

    #ifdef CS333_P3
    stateListRemove(&ptable.list[EMBRYO], p);
    assertState(p, EMBRYO, __FUNCTION__, __LINE__);
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
    #endif

    release(&ptable.lock);
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  #ifdef CS33_P1
  p->start_ticks = ticks;
  #endif
  
  #ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  #endif

  #ifdef DEBUG
  checkProcs(__FILE__, __FUNCTION__, __LINE__);
  #endif // Debug

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  acquire(&ptable.lock);
  #ifdef CS333_P3
  initProcessLists();
  initFreeList();
  #endif
  release(&ptable.lock);

  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  #ifdef CS333_P2
  p->uid = DEF_UID;
  p->gid = DEF_GID;
  p->parent = p;
  #endif


  #ifdef CS333_P3
  stateListRemove(&ptable.list[EMBRYO], p);
  assertState(p, EMBRYO, __FUNCTION__, __LINE__);
  p->state = RUNNABLE;
  stateListAdd(&ptable.list[RUNNABLE], p);
  #endif

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    acquire(&ptable.lock);
    kfree(np->kstack);
    np->kstack = 0;

    #ifdef CS333_P3
    stateListRemove(&ptable.list[EMBRYO], np);
    assertState(np, EMBRYO, __FUNCTION__, __LINE__);
    np->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], np);
    #endif

    release(&ptable.lock);
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  np->uid = curproc->uid;
  np->gid = curproc->gid;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  #ifdef CS333_P3
  stateListRemove(&ptable.list[EMBRYO], np);
  assertState(np, EMBRYO, __FUNCTION__, __LINE__);
  np->state = RUNNABLE;
  stateListAdd(&ptable.list[RUNNABLE], np);
  #endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifdef CS333_P3 // New exit()
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.

  #ifdef CS333_P3
  stateListRemove(&ptable.list[RUNNING], curproc);
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = ZOMBIE;
  stateListAdd(&ptable.list[ZOMBIE], curproc);
  #endif
  //release(&ptable.lock);

  #ifdef PDX_XV6
  curproc->sz = 0;
  #endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#else // Old exit()
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  #ifdef PDX_XV6
  curproc->sz = 0;
  #endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#endif

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P3 // New wait()
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through zombie table looking for children.
    havekids = 0;
    for(enum procstate state = EMBRYO; state <= ZOMBIE; ++state){
      for(p = ptable.list[state].head; p; p = p->next){
        if(p->parent != curproc)
          continue;
        havekids = 1;
        if(p->state == ZOMBIE){
          // Found one.
          pid = p->pid;
          kfree(p->kstack);
          p->kstack = 0;
          freevm(p->pgdir);
          p->pid = 0;
          p->parent = 0;
          p->name[0] = 0;
          p->killed = 0;

          stateListRemove(&ptable.list[ZOMBIE], p);
          assertState(p, ZOMBIE, __FUNCTION__, __LINE__);
          p->state = UNUSED;
          stateListAdd(&ptable.list[UNUSED], p);

          release(&ptable.lock);
          return pid;
        }
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#else // Old wait()
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#endif

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifdef CS333_P3 // New scheduler()
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  #ifdef PDX_XV6
  int idle;  // for checking if processor is idle
  #endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

    #ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
    #endif // PDX_XV6
    // Grab first process out of the RUNNABLE table for a process to run.
    acquire(&ptable.lock);
    if(ptable.list[RUNNABLE].head){
      p = ptable.list[RUNNABLE].head;
      if(p->state != RUNNABLE) continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      #ifdef PDX_XV6
      idle = 1;  // not idle this timeslice
      #endif // PDX_XV6

      c->proc = p;
      switchuvm(p);

      stateListRemove(&ptable.list[RUNNABLE], p);
      assertState(p, RUNNABLE, __FUNCTION__, __LINE__);
      p->state = RUNNING;
      stateListAdd(&ptable.list[RUNNING], p);

      p->cpu_ticks_in = ticks;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
    #ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
    #endif // PDX_XV6
  }
}
#else // Old scheduleer()*/
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  #ifdef PDX_XV6
  int idle;  // for checking if processor is idle
  #endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

    #ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
    #endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      #ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
      #endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      p->cpu_ticks_in = ticks;
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
    #ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
    #endif // PDX_XV6
  }
}
#endif

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  p->cpu_ticks_total += (ticks - p->cpu_ticks_in);
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
#ifdef CS333_P3 // New yield()
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock

  #ifdef CS333_P3
  stateListRemove(&ptable.list[RUNNING], curproc);
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);
  curproc->state = RUNNABLE;
  stateListAdd(&ptable.list[RUNNABLE], curproc);
  #endif

  sched();
  release(&ptable.lock);
}
#else // Old yield()
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
#endif

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
#ifdef CS333_P3 // New sleep()
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;

  stateListRemove(&ptable.list[RUNNING], p);
  assertState(p, RUNNING, __FUNCTION__, __LINE__);
  p->state = SLEEPING;
  stateListAdd(&ptable.list[SLEEPING], p);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#else // Old sleep()
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#endif

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifdef CS333_P3 // New wakeup1()
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.list[SLEEPING].head; p; p = p->next){
    if(p->chan == chan){
      stateListRemove(&ptable.list[SLEEPING], p);
      assertState(p, SLEEPING, __FUNCTION__, __LINE__);
      p->state = RUNNABLE;
      stateListAdd(&ptable.list[RUNNABLE], p);
    }
  }
}
#else // Old wakeup1()
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#endif

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
#ifdef CS333_P3 // New kill()
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(enum procstate state = EMBRYO; state <= RUNNING; ++state){
    for(p = ptable.list[state].head; p; p = p->next){
      if(p->pid == pid){
        p->killed = 1;
        // Wake process from sleep if necessary.
        if(p->state == SLEEPING){
          stateListRemove(&ptable.list[SLEEPING], p);
          assertState(p, SLEEPING, __FUNCTION__, __LINE__);
          p->state = RUNNABLE;
          stateListAdd(&ptable.list[RUNNABLE], p);
        }
        release(&ptable.lock);
        return 0;
      }
    }
  }
  release(&ptable.lock);
  return -1;
}
#else // Old kill()
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
#endif

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

#ifdef CS333_P1
void
procdumpP1(struct proc* p, char* state)
{
  uint et = ticks - p->start_ticks; // Elapsed time
  uint second = et / 1000;
  uint decimal = et - (second * 1000);
  char* padding;
  if(decimal < 10) padding = "00";
  else if(decimal < 100) padding = "0";
  else padding = "";

  cprintf("%d\t%s\t\t%d.%s%d\t\t%s\t%d", p->pid, p->name, second, padding, decimal, state, p->sz);
}
#endif

#ifdef CS333_P2
void
procdumpP2(struct proc* p, char* state)
{
  uint ppid = 0;
  if(p->parent) ppid = p->parent->pid;

  uint et = ticks - p->start_ticks; // Elapsed time
  uint eseconds = et / 1000;
  uint edecimal = et - (eseconds * 1000);
  char* epadding;
  if(edecimal < 10) epadding = "00";
  else if(edecimal < 100) epadding = "0";
  else epadding = "";

  uint ct = p->cpu_ticks_total; // CPU time
  uint cseconds = ct / 1000;
  uint cdecimal = ct - (cseconds * 1000);
  char* cpadding;
  if(cdecimal < 10) cpadding = "00";
  else if(cdecimal < 100) cpadding = "0";
  else cpadding = "";
 
  cprintf("%d\t%s\t     %d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\t",
    p->pid,
    p->name,
    p->uid,
    p->gid,
    ppid,
    eseconds,
    epadding,
    edecimal,
    cseconds,
    cpadding,
    cdecimal,
    state,
    p->sz);
}
#endif

#ifdef CS333_P3
void
procdumpP3(struct proc* p, char* state){
  uint ppid = 0;
  if(p->parent) ppid = p->parent->pid;

  uint et = ticks - p->start_ticks; // Elapsed time
  uint eseconds = et / 1000;
  uint edecimal = et - (eseconds * 1000);
  char* epadding;
  if(edecimal < 10) epadding = "00";
  else if(edecimal < 100) epadding = "0";
  else epadding = "";

  uint ct = p->cpu_ticks_total; // CPU time
  uint cseconds = ct / 1000;
  uint cdecimal = ct - (cseconds * 1000);
  char* cpadding;
  if(cdecimal < 10) cpadding = "00";
  else if(cdecimal < 100) cpadding = "0";
  else cpadding = "";
 
  cprintf("%d\t%s\t     %d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\t",
    p->pid,
    p->name,
    p->uid,
    p->gid,
    ppid,
    eseconds,
    epadding,
    edecimal,
    cseconds,
    cpadding,
    cdecimal,
    state,
    p->sz);
}
#endif

void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

#if defined(CS333_P4)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P3)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName            Elapsed\t\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif

  cprintf(HEADER);  // not conditionally compiled as must work in all project states

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    #if defined(CS333_P3)
    procdumpP3(p, state);
    #elif defined(CS333_P2)
    procdumpP2(p, state);
    #elif defined(CS333_P1)
    procdumpP1(p, state);
    #else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
    #endif

    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
#ifdef CS333_P1
  cprintf("$ ");  // simulate shell prompt
#endif // CS333_P1
}

#ifdef CS333_P3
void
readydump(void){
  struct proc* p;

  cprintf("\nReady List Processes:\n");
  for(p = ptable.list[RUNNABLE].head; p; p = p->next){
    cprintf("%d", p->pid);
    if(p->next) cprintf(" -> ");
  }
  cprintf("\n$ ");
}

void
freedump(void){
  uint i = 0;
  for(struct proc* p = ptable.list[UNUSED].head; p; p = p->next){ ++i; }
  cprintf("Free List Size: %d\n$ ", i);
}

void
sleepdump(void){
  cprintf("\nSleep List Processes:\n");
  for(struct proc* p = ptable.list[SLEEPING].head; p; p = p->next){
    cprintf("%d", p->pid);
    if(p->next) cprintf(" -> ");
  }
  cprintf("\n$ ");
}

void
zombiedump(void){
  cprintf("\nZombie List Processes:\n");
  for(struct proc* p = ptable.list[ZOMBIE].head; p; p = p->next){
    cprintf("(%d, %d)", p->pid, p->parent->pid);
    if(p->next) cprintf(" -> ");
  }
  cprintf("\n$ ");
}
#endif

#ifdef CS333_P2
int
getproc(uint max, struct uproc* table)
{
  uint i;
  struct proc* p;

  acquire(&ptable.lock);
  for(i = 0, p = ptable.proc; i < max && p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->state == EMBRYO) continue;

    table[i].pid = p->pid;
    table[i].uid = p->uid;
    table[i].gid = p->gid;
    if(!p->parent) table[i].ppid = 0;
    else table[i].ppid = p->parent->pid;
    table[i].elapsed_ticks = (ticks - p->start_ticks);
    table[i].CPU_total_ticks = p->cpu_ticks_total;
    table[i].size = p->sz;

    safestrcpy(table[i].name, p->name, strlen(p->name) + 1);
    safestrcpy(table[i].state, states[p->state], strlen(states[p->state]) + 1);
    if(p->pid != 0) ++i;
  }
  release(&ptable.lock);

  return i;
}
#endif

#ifdef CS333_P3
// list management helper functions
static void
stateListAdd(struct ptrs* list, struct proc* p) {
  if((*list).head == NULL){
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  } else{
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}

static int
stateListRemove(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL || (*list).tail == NULL || p == NULL){
    return -1;
  }

  struct proc* current = (*list).head;
  struct proc* previous = 0;

  if(current == p){
    (*list).head = ((*list).head)->next;
    // prevent tail remaining assigned when we've removed the only item
    // on the list
    if((*list).tail == p){
      (*list).tail = NULL;
    }
    return 0;
  }

  while(current){
    if(current == p){
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found. return error
  if(current == NULL){
    return -1;
  }

  // Process found.
  if(current == (*list).tail){
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  } else{
    previous->next = current->next;
  }

  // Make sure p->next doesn't point into the list.
  p->next = NULL;

  return 0;
}

static void
initProcessLists(){
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++) {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
  
  #ifdef CS333_P4
  for (i = 0; i <= MAXPRIO; i++) {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
  #endif
}

static void
initFreeList(void) {
  struct proc* p;

  for(p = ptable.proc; p < ptable.proc + NPROC; ++p){
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}

// example usage:
// assertState(p, UNUSED, __FUNCTION__, __LINE__);
// This code uses gcc preprocessor directives. For details, see
// https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html
static void
assertState(struct proc *p, enum procstate state, const char * func, int line) {
  if (p->state == state) return;
  cprintf("Error: proc state is %s and should be %s.\nCalled from %s line %d\n", states[p->state], states[state], func, line);
  panic("Error: Process state incorrect in assertState()");
}
#endif

// debug routines
#ifdef DEBUG
static int
procLookup(struct proc *p, struct proc *np){
  while (np != NULL) {
    if (np == p) return 1;
    np = np->next;
  }
  return 0;
}

static int
findProc(struct proc *p){
  if (procLookup(p, ptable.list[UNUSED].head)   != 0) return 1;
  if (procLookup(p, ptable.list[EMBRYO].head)   != 0) return 1;
  if (procLookup(p, ptable.list[RUNNING].head)  != 0) return 1;
  if (procLookup(p, ptable.list[SLEEPING].head) != 0) return 1;
  if (procLookup(p, ptable.list[ZOMBIE].head)   != 0) return 1;
  for (int i=0; i<=statecount; i++)
  if (procLookup(p, ptable.ready[i].head) != 0) return 1;

  return 0; // not found
}

static void
checkProcs(const char *file, const char *func, int line){
  int found;
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    found = findProc(p);
    if (found) continue;
    cprintf("checkprocs error. Called from %s, %s, @ %d\n", file, func, line);
    panic("Process array and lists inconsistent\n");
  }
}
#endif
