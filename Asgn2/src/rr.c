#include "../include/lwp.h"
#include <stdlib.h>
#include <stdio.h>

/* scheduler manipulates a doubly linked list by maintaining a
queue of threads to be executed. Top of queue is next to be run */
thread head = NULL;
thread tail = NULL;

void rr_admit(thread new)
{
    if (head == NULL && tail == NULL)
    {
        head = new;
        tail = new;
        return;
    }

    // add to end of queue
    tail->next = new;
    new->prev = tail;
    tail = new;
}

void rr_remove(thread victim)
{
    /* begin searching for victim starting at tail */
    thread temp = tail;
    while (temp != NULL && temp->tid != victim->tid)
    {
        temp = temp->prev;
    }

    /* if tail is NULL or no victim found */
    if (temp == NULL || temp->tid != temp->tid)
    {
        return;
    }

    /* victim found (aka temp) so remove temp (doubly linked list so set BOTH 'next' and 'prev' pointer for each node) */
    // setting next pointer
    if (temp->prev != NULL)
    {
        // set 'next' pointer for thread initally pointing to victim to now point to thread after victim
        temp->prev->next = temp->next;
    }
    else
    {
        // no thread before victim so thread after victim is head
        head = temp->next;
    }
    // set previous pointer
    if (temp->next != NULL)
    {
        // set 'prev' pointer for thread initially pointing to victim to now point thread before victim
        temp->next->prev = temp->prev;
    }
    else
    {
        // no thread after victim so thread before victim is tail
        tail = temp->prev;
    }
}

thread rr_next(void)
{
    thread thread_to_run;

    /* no threads to run if head is null */
    if (head == NULL)
    {
        return NULL;
    }

    /* dequeue first in queue to return as next thread to run */
    thread_to_run = head;
    head = head->next;
    if (head != NULL)
    {
        // ensure second thread in queue 'prev' pointer does not point back to former head (aka thread_to_run)
        head->prev = NULL;
    }
    else
    {
        // thread_to_run was last thread so both head and tail should be NULL
        tail = NULL;
    }

    /* reset thread_to_run position pointers */
    thread_to_run->next = NULL;
    thread_to_run->prev = NULL;

    /* cycle dequeued thread to end of queue for RR */
    rr_admit(thread_to_run);

    return thread_to_run;
}