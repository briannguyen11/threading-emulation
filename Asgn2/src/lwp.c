#define _GNU_SOURCE
#include "lwp.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/mman.h>

// global variables
unsigned int next_tid = 0; // counter for tid

rfile main_ctx; // saves main stack context before thread execution

thread thread_internal = NULL; // head of local double linked list
thread thread_curr = NULL;     // thread being executed

static struct scheduler round_robin = {NULL, NULL, rr_admit, rr_remove, rr_next};
scheduler sched = &round_robin;

thread *wait_queue;
int wait_add_idx = 0;
int wait_rmx_idx = 0;

/******************** Support Functions *******************/
/*
 * Description: wrapper to take in thread function and args for thread function
 * Params: lwpfun fun and void *arg
 * Return: void
 */
static void lwp_wrap(lwpfun fun, void *arg)
{
    int rval;
    rval = fun(arg);
    lwp_exit(rval);
}

/******************** Main Functions *******************/

/*
 * Description: creates thread struct with setup stack
 * Params: lwpfun funnction and void *arguments
 * Return: tid of newly created thread
 */
tid_t lwp_create(lwpfun function, void *argument)
{
    thread new_thread;

    /* init new thread */
    new_thread = (thread)malloc(sizeof(context));
    if (!new_thread)
    {
        perror("lwp_create");
        return (tid_t)-1;
    }

    /* set stack size (default to 8MB) */
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    struct rlimit rlim;
    if (getrlimit(RLIMIT_STACK, &rlim) == 0 && rlim.rlim_cur != RLIM_INFINITY)
    {
        new_thread->stacksize = ((rlim.rlim_cur + page_size - 1) / page_size) * page_size;
    }
    else
    {
        new_thread->stacksize = ((DEFAULT_STACK_SIZE + page_size - 1) / page_size) * page_size;
    }

    /* allocate memory for the stack (mmap returns addr to new allocated stack) */
    new_thread->stack = mmap(NULL, new_thread->stacksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (!new_thread->stacksize)
    {
        perror("lwp_create");
        return (tid_t)-1;
    }

    /* set tid */
    next_tid++;
    new_thread->tid = next_tid;

    /* set addresses in stack */
    unsigned long *stack_ptr = new_thread->stack + (new_thread->stacksize / sizeof(unsigned long)); // bottom of stack
    if ((uintptr_t)stack_ptr % 16 != 0)
    {
        stack_ptr = (unsigned long *)((uintptr_t)stack_ptr - ((uintptr_t)stack_ptr % 16)); // ensure 16-byte alignment
    }
    stack_ptr--;
    *stack_ptr = (unsigned long)lwp_wrap; // decrem by 1 moves 8 bytes
    stack_ptr--;                          // set addr of curr sp in stack ?

    /* set up context registers */
    new_thread->state.rbp = (unsigned long)stack_ptr;
    new_thread->state.rsp = (unsigned long)stack_ptr;
    new_thread->state.rdi = (unsigned long)function;
    new_thread->state.rsi = (unsigned long)argument;
    new_thread->state.fxsave = FPU_INIT;

    /* init pointers for internal doubly linked list (will not need linked_list.c)
    and prev, next, etc. are #defines in .h */
    new_thread->prev = NULL;
    new_thread->next = NULL;
    new_thread->right = NULL;
    new_thread->left = NULL;

    /* schedule new thread */
    sched->admit(new_thread);

    /* inserting into local doubly linked list */
    if (thread_internal == NULL)
    {
        thread_internal = new_thread;
    }
    else
    {
        thread_internal->left = new_thread;
        new_thread->right = thread_internal;
        thread_internal = new_thread; // this else body puts new_thread in the front of current head
    }

    return new_thread->tid;
}

/*
 * Description: begins execution of threads
 * Params: void
 * Return: void
 */
void lwp_start(void)
{
    /* init wait queue */
    wait_queue = malloc(sizeof(thread) * next_tid);

    /* ensure lwp start called after lwp_create() */
    if (thread_curr != NULL && next_tid == 1)
    {
        return;
    }

    /* get thread to execute from sched */
    thread_curr = sched->next();

    /* if current thread is NULL, return to main proccess */
    if (thread_curr == NULL)
    {
        swap_rfiles(NULL, &main_ctx);
        return; // should not reach bc stack pointer points somewhere else
    }

    /* otherwise, save original context and switch to new thread */
    swap_rfiles(&main_ctx, &thread_curr->state);
}

/*
 * Description: gives up execution to another thread via context switch
 * Params: void
 * Return: void
 */
void lwp_yield(void)
{
    thread thread_former_curr;

    /* move to new thread to execute */
    thread_former_curr = thread_curr;
    thread_curr = sched->next();

    /* if current thread is NULL, return to main proccess */
    if (thread_curr == NULL)
    {
        swap_rfiles(NULL, &main_ctx);
        return; // should not reach bc stack pointer points somewhere else
    }

    /* otherwise, save former thread context and switch to new thread */
    swap_rfiles(&thread_former_curr->state, &thread_curr->state);
}

/*
 *Description : terminates current thread and goes to next thread
 *Params : void
 *Return : void
 */
void lwp_exit(int exitval)
{
    if (thread_curr != NULL)
    {
        thread thread_finished_curr;

        thread_finished_curr = thread_curr;

        wait_queue[wait_add_idx] = thread_finished_curr;
        wait_add_idx++;

        sched->remove(thread_finished_curr);

        thread_curr = sched->next();

        /* if current thread is NULL, return to main proccess */
        if (thread_curr == NULL)
        {
            swap_rfiles(NULL, &main_ctx);
            return; // should not reach bc stack pointer points somewhere else
        }

        /* otherwise, save former thread context and switch to new thread */
        swap_rfiles(NULL, &thread_curr->state);
    }
}

/*
 *Description : cleans up terminated threads
 *Params : int status
 *Return : tid_t tid
 */
tid_t lwp_wait(int *status)
{
    /* get top of wait_queue */
    thread thread_terminated = wait_queue[wait_rmx_idx];
    wait_rmx_idx++;

    if (thread_terminated == NULL)
    {
        printf("%s", "what is going on");
    }

    if (munmap(thread_terminated->stack, thread_terminated->stacksize) == -1)
    {
        perror("munmap");
        return 1;
    }
    status = thread_terminated->status;
    return thread_terminated->tid;
}

/*
 *Description : returns tid of calling thread
 *Params : none
 *Return : tid_t tid
 */
tid_t lwp_gettid(void)
{
    if (thread_curr == NULL)
    {
        return NO_THREAD;
    }
    else
    {
        return thread_curr->tid;
    }
}

/*
 *Description : returns thread given its tid
 *Params : tid_t tid
 *Return : thread
 */
thread tid2thread(tid_t tid)
{
    thread thread_return;

    thread_return = thread_internal;

    while (thread_return != NULL)
    {
        if (thread_return->tid == tid)
        {
            return thread_return;
        }
        else
        {
            thread_return = thread_return->right;
            return NULL;
        }
    }
}
/*
 *Description : returns a pointer to current scheduler
 *Params : void
 *Return : scheduler
 */
scheduler lwp_get_scheduler(void)
{
    return sched;
}

/*
 *Description : sets the current scheduler to sched and moves all
 * threadsto new scheduler
 *Params : void
 *Return : scheduler
 */
void lwp_set_scheduler(scheduler sched)
{
    if (sched != NULL)
    {
    }
    else
    {
    }
}