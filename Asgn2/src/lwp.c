#include "../include/lwp.h"
#include "../include/linked_list.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/mman.h>

// global variables
unsigned int next_tid = 0; // for setting tid
thread thread_head = NULL;

/******************** Support Functions *******************/

static void lwp_wrap(lwpfun fun, void *arg)
{
    int rval;
    rval = fun(arg);
    lwp_exit(rval);
}

/******************** Main Functions *******************/

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
    *(stack_ptr--) = (unsigned long)stack_ptr;

    /* set up context registers */
    new_thread->state.rbp = (unsigned long)stack_ptr;
    new_thread->state.rsp = (unsigned long)stack_ptr;
    new_thread->state.rdi = (unsigned long)function;
    new_thread->state.rsi = (unsigned long)argument;
    new_thread->state.fxsave = FPU_INIT;

    /* init thread links */
    new_thread->prev = NULL;
    new_thread->next = NULL;
    new_thread->right = NULL;
    new_thread->left = NULL;

    /* set up internal doubly linked list */
    if (!thread_head)
    {
        thread_head = new_thread;
    }
    else
    {
        thread_head->left = new_thread;
        new_thread->right = thread_head;
        thread_head = new_thread;
    }

    return new_thread->tid;
}

int main(int argc, char *argv[])
{
    return 0;
}