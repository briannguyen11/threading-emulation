#include "../include/lwp.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <unistd.h>

// global variables
unsigned int next_tid = 0; // counter for tid

rfile main_ctx; // saves main stack context before thread execution

thread thread_head = NULL; // head of local double linked list
thread thread_curr = NULL; // thread being executed

static struct scheduler round_robin = {NULL, NULL, rr_admit, rr_remove, rr_next};
scheduler sched = &round_robin;

/******************** Support Functions *******************/

static void lwp_wrap(lwpfun fun, void *arg)
{
    int rval;
    rval = fun(arg);
    lwp_exit(rval);
}

/******************** Main Functions *******************/

/*
 * Description: creates lwp struct with setup stack
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

    /* allocate memory for the stack (mmap returns addr to new allocated stack) */
    new_thread->stack = mmap(NULL, DEFAULT_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (!new_thread->stacksize)
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

    /* set tid */
    new_thread->tid = next_tid++;

    /* set addresses in stack */
    unsigned long *stack_ptr = new_thread->stack + new_thread->stacksize; // assuming 16 byte aligned
    *(stack_ptr--) = (unsigned long)lwp_wrap;                             // decrem by 1 moves 8 bytes
    *(stack_ptr--) = (unsigned long)stack_ptr;                            // set addr of curr sp in stack ?

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
    if (thread_head == NULL)
    {
        thread_head = new_thread;
    }
    else
    {
        thread_head->left = new_thread;
        new_thread->right = thread_head;
        thread_head = new_thread; // this else body puts new_thread in the front of current head
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
 * Description: yeilds execution to another LWP
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
    swap_rfiles(&thread_former_curr, &thread_curr->state);
}


void lwp_exit(int exitval)
{
    if (thread_curr != NULL)
    {
        sched->remove(thread_curr);

        /* Set stack to safe memory location */
        unsigned long *safe_stack = thread_curr->stack;
        curr->state.rsp = (uint64_t)safe_stack;

        /* make sure thread is terminated before deallocation */
        if (lwp_wait() != NULL)
        {
            /* schedule and yield to next thread */
            thread_curr = sched->next();
            lwp_yield();
        }
    }
    else
    {
        /* Swap to main process context if no thread running */
        swap_rfiles(&thread_curr->state, &main_ctx);
    }
}


tid_t lwp_wait(int *status)
{
    /* deallocates resources for terminated thread */
    tid_t temp_tid = lwp_gettid();
    free(thread_curr->stack);
    free(thread_curr);

    if (thread_curr == NULL)
    {
        /* Swap to main process context if no thread running */
        swap_rfiles(&thread_curr->state, &main_ctx);
    }
    return temp_tid;
}



int main(int argc, char *argv[])
{
    return 0;
}