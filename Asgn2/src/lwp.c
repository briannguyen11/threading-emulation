#define _GNU_SOURCE
#include "lwp.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/mman.h>

// global variables
unsigned int tid_cnt = 0; // counter for tid

rfile main_ctx; // saves main stack context before thread execution

thread thread_internal = NULL; // head of local double linked list
thread thread_curr = NULL;     // thread being executed

static struct scheduler round_robin = {NULL, NULL, rr_admit, rr_remove, rr_next};
scheduler sched = &round_robin;

thread wait_queue_first = NULL;
thread wait_queue_last = NULL;

thread *terminated_queue;
int terminated_add_idx;
int terminated_rmv_idx;

int last_exit_flg = 0;

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
    tid_cnt++;
    new_thread->tid = tid_cnt;
    new_thread->status = LWP_LIVE;

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
    terminated_queue = malloc(sizeof(thread) * tid_cnt);

    /* init main thread */
    thread main_thread = (thread)malloc(sizeof(context));
    main_thread->tid = 0;
    main_thread->state = main_ctx;
    sched->admit(main_thread);

    /* ensure lwp start called after lwp_create() */
    if (thread_curr != NULL && tid_cnt == 1)
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
    swap_rfiles(&main_thread->state, &thread_curr->state);
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
        // printf("from yield\n");
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
void lwp_exit(int status)
{
    if (thread_curr != NULL)
    {
        if (thread_curr->tid != 0)
        {
            thread thread_finished_curr;
            thread_finished_curr = thread_curr;

            /* if there is a waiting thread, take the top and store in eixiting threads exitied ptr */
            if (wait_queue_first != NULL)
            {
                thread rmv_assoc_waiting_thread = wait_queue_first;
                wait_queue_first = wait_queue_first->right;
                if (wait_queue_first != NULL)
                {
                    wait_queue_first->left = NULL;
                }
                else
                {
                    wait_queue_last = NULL;
                }
                // thread_finished_curr->exited = rmv_assoc_waiting_thread;
                rmv_assoc_waiting_thread->exited = thread_finished_curr;
                sched->admit(rmv_assoc_waiting_thread);
            }

            /* update status of removed thread */
            thread_finished_curr->status = MKTERMSTAT(LWP_TERM, status);

            /* pushed thread to remove onto waiting queue */
            terminated_queue[terminated_add_idx] = thread_finished_curr;
            terminated_add_idx++;

            /* remove thread */
            sched->remove(thread_finished_curr);

            /* scehdule new thread */
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

        if (last_exit_flg == 1 && thread_curr->tid == 0)
        {
            free(thread_curr);
        }
    }
}

/*
 *Description : cleans up terminated threads
 *Params : int status
 *Return : tid_t tid
 */
tid_t lwp_wait(int *status)
{
    // printf("called wait\n");
    int terminated_id = NO_THREAD;

    /* if not threads in terminated queue, put current thread into waiting queue */
    if ((terminated_add_idx - terminated_rmv_idx) == 0)
    {
        sched->remove(thread_curr); // removed from main sched

        if (wait_queue_first == NULL && wait_queue_last == NULL)
        {
            wait_queue_first = thread_curr;
            wait_queue_last = thread_curr;
        }
        else
        {
            wait_queue_last->right = thread_curr;
            thread_curr->left = wait_queue_last;
            wait_queue_last = thread_curr;
        }

        /* context switch to new thread */
        lwp_yield();

        /* returns here */
        if (thread_curr->exited != NULL)
        {
            /* remove itself from sched */
            sched->remove(thread_curr);

            /* top of terminated queue */
            thread thread_terminated = thread_curr->exited;
            terminated_id = thread_terminated->tid;

            // adjust terminated queue
            terminated_rmv_idx++;
            tid_cnt--;

            /* clean up allocated stack for specific thread */
            if (munmap(thread_terminated->stack, thread_terminated->stacksize) == -1)
            {
                perror("munmap");
                return 1;
            }

            /* update exit status */
            if (status != NULL)
            {
                *status = thread_terminated->status;
            }

            /* clean up thread */
            free(thread_terminated);

            /* clean up allocated wait queue */
            if (tid_cnt == 0 && (terminated_add_idx - terminated_rmv_idx) == 0)
            {
                free(terminated_queue);
                last_exit_flg = 1;
            }
            return terminated_id;
        }
    }
    else
    {
        /* get top of terminated_queue and keep track of indices for wait queue */
        thread thread_terminated = terminated_queue[terminated_rmv_idx];

        printf("something in terminate: %d\n", thread_terminated->tid);

        terminated_id = thread_terminated->tid;
        terminated_rmv_idx++;
        tid_cnt--;

        /* clean up allocated stack for specific thread */
        if (munmap(thread_terminated->stack, thread_terminated->stacksize) == -1)
        {
            perror("munmap");
            return 1;
        }

        /* update exit status */
        if (status != NULL)
        {
            *status = thread_terminated->status;
        }

        /* clean up thread */
        free(thread_terminated);
    }

    /* clean up allocated wait queue */
    printf("%d", terminated_add_idx - terminated_rmv_idx);
    if (tid_cnt == 0 && (terminated_add_idx - terminated_rmv_idx) == 0)
    {
        free(terminated_queue);
        printf("I set the flag\n");
        last_exit_flg = 1;
    }
    return terminated_id;
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
        }
    }
    return NULL;
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
 *Description : sets the current scheduler to new_sched
 * and moves all threads to new scheduler
 *Params : scheduler
 *Return : void
 */
void lwp_set_scheduler(scheduler new_sched)
{
    thread thread_move;

    /* default to round robin */
    if (new_sched->init == NULL)
    {
        new_sched = &round_robin;
    }

    /* initialize new scheduler */
    new_sched->init();

    /* move each thread from old to new scheduler */
    while (thread_move = new_sched->next())
    {
        sched->remove(thread_move);
        new_sched->admit(thread_move);
    }

    /* shutdown old scheduler */
    if (sched->shutdown != NULL)
    {
        sched->shutdown();
    }

    sched = new_sched;
}