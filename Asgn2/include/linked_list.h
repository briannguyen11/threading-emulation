#ifndef LINKED_LIST_H
#define LINKED_LIST_H
#include "lwp.h"

typedef struct Node
{
    context data;
    struct Node *next;
} Node;

Node *createNode(context data);
void insertEnd(Node **head, context data);
void displayList(Node *head);
void freeList(Node *head);

#endif // LINKED_LIST_H