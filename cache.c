#include "cache.h"
#include <unistd.h>

//insert beginning of list. Return new head
Node* push(Node* head, char *url, int lenbytes, unsigned char *bytes)
{
   Node* insertMe = (struct Node*)malloc(sizeof(Node));
   insertMe->url  = url;
   insertMe->nbytes = lenbytes;
   insertMe->bytes = bytes;
   insertMe->next = NULL;
   insertMe->prev = NULL;
   head = toFront(head, insertMe);
   return head;
}

Node* toFront(Node* head, Node* moveMe)
{
   //rewire if moveMe was an existing node
   if(moveMe->prev != NULL) //if it is = NULL, then moveMe is a new node
   {
       printf("Move me was existing node apparently\n");
       moveMe->prev->next = moveMe->next;
       moveMe->next->prev = moveMe->prev;
   }
   moveMe->next = (head == NULL) ? moveMe : head;
   moveMe->prev = (head == NULL) ? moveMe : head->prev;
   if(head != NULL)
   {
       head->prev->next = moveMe; //get wrap around
       head->prev = moveMe;
   }
   head = moveMe;
   return head;
}

void pop(Node* tail) //TODO not done
{
    if(tail == NULL)
        return;
    Node *head = tail->next;
    head->prev = tail->prev;
    tail->prev->next = head;
    free(tail->bytes);
    free(tail);
}

void printLL(Node *head)
{
    if(head == NULL)
    {
        printf("List is empty\n");
        return;
    }
    Node *curr = head;
    while (1)
    {
        printNode(curr);
        curr = curr->next;
        if(curr == head)
            break;
    }
}

void printNode(Node *node)
{
    printf("------Node begin------\n");
    printf("URL: %s\nnbytes: %d\nBytes:\n%s",node->url, node->nbytes, node->bytes);
    printf("-------Node end-------\n");
}
