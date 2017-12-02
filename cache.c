#include "cache.h"
#include <unistd.h>

int curr_cache_size = 0;

//insert beginning of list. Return new head
Node* push(Node* head, char *url, int nbytes, unsigned char *bytes)
{
    Node* insertMe = (struct Node*)malloc(sizeof(Node));
    insertMe->url  = url;
    insertMe->nbytes = nbytes;
    insertMe->bytes = bytes;
    insertMe->next = NULL;
    insertMe->prev = NULL;

    head = toFront(head, insertMe);
    curr_cache_size += nbytes;
    printf("Cache size increased to %d\n", curr_cache_size);
    //if you're cache is over the limit, remove the tail until it's not
    while(curr_cache_size > MAX_CACHE_SIZE)
    {
        printf("Cache size has gone over limit, popping\n");
        pop(head->prev);
    }
    return (curr_cache_size > 0) ? head : NULL; //if you pop until the cache is empty, need to set head to null.
                                                //I don't think this can occur since MAX cache is so much bigger
                                                //than MAX object, but for debug purposes it was causing problems
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

void pop(Node* tail)
{
    if(tail == NULL)
        return;
    Node *head = tail->next;
    head->prev = tail->prev;
    tail->prev->next = head;
    curr_cache_size -= tail->nbytes;
    printf("Cache size reduced to %d\n", curr_cache_size);
    free(tail->bytes);
    free(tail);
    tail = NULL;
}

//returns the node in the LL with the given url. If no such node, returns NULL
Node* find(Node* head, char *url)
{
    if(head == NULL)
        return NULL;
    Node* curr = head;
    printf("Given url: %s\n",url);
    printf("curr url: %s\n",curr->url);
    while(strcmp(curr->url, url)) //while curr url isn't equal to given url
    {
        printf("curr url: %s\n",curr->url);
        curr = curr->next;
        if(curr == head)
        {
            curr = NULL;
            break;
        }
    }

    return curr;
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
    if(node == NULL) {
        printf("Node is NULL\n");
        return;
    }

    printf("------Node begin------\n");
    printf("URL: %s\nnbytes: %d\nBytes:\n%s",node->url, node->nbytes, node->bytes);
    printf("-------Node end-------\n");
}
