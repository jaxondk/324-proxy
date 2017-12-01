#ifndef __CACHE_H__
#define __CACHE_H__

#include<stdio.h>
#include<stdlib.h>
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct Node {
    char *url;
    int nbytes;
    unsigned char *bytes;
    struct Node *next;
    struct Node *prev;
} Node;

Node* push(Node* head, char *url, int nbytes, unsigned char *bytes);
Node* toFront(Node* head, Node* moveMe);
void pop(Node* tail); //TODO not done
void printLL(Node *head);
void printNode(Node *node);

#endif /* __CACHE_H__ */
