#include "../include/lwp.h"
#include "../include/linked_list.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/mman.h>

// global variables
unsigned int next_tid = 0;

/******************** Support Functions *******************/

/******************** Main Functions *******************/

tid_t lwp_create(lwpfun function, void *argument)
{
    context curr_lwp;
    curr_lwp.tid = next_tid++;
    curr_lwp.stacksize = DEFAULT_STACK_SIZE;
    curr_lwp.stack = mmap(NULL, DEFAULT_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_STACK, -1, 0);
    ;
    return 0;
}

int main(int argc, char *argv[])
{

    Node *head = NULL;

    context thread1 = {
        .tid = 1,
        .stack = NULL,
        .stacksize = 0,
        .state = NULL,
        .status = 0,
        .lib_one = NULL,
        .lib_two = NULL,
        .sched_one = NULL,
        .sched_two = NULL,
        .exited = NULL};

    insertEnd(&head, thread1);

    printf("Linked list: \n");
    displayList(head);
    freeList(head);

    return 0;
}