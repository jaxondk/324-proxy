#ifndef __SBUF_H__
#define __SBUF_H__

#include "csapp.h"

/* $begin sbuft */
typedef struct {
    int *buf;          /* Buffer array */
    int n;             /* Maximum number of slots */
    int front;         /* buf[(front+1)%n] is first item */
    int rear;          /* buf[rear%n] is last item */
    sem_t mutex;       /* Protects accesses to buf */
    sem_t slots;       /* Counts available slots */
    sem_t items;       /* Counts available items */
} int_sbuf_t;
/* $end sbuft */

typedef struct {
    char **buf;          /* Buffer array */
    int n;             /* Maximum number of slots */
    int front;         /* buf[(front+1)%n] is first item */
    int rear;          /* buf[rear%n] is last item */
    sem_t mutex;       /* Protects accesses to buf */
    sem_t slots;       /* Counts available slots */
    sem_t items;       /* Counts available items */
} str_sbuf_t;

void int_sbuf_init(int_sbuf_t *sp, int n);
void int_sbuf_deinit(int_sbuf_t *sp);
void int_sbuf_insert(int_sbuf_t *sp, int item);
int int_sbuf_remove(int_sbuf_t *sp);

void str_sbuf_init(str_sbuf_t *sp, int n);
void str_sbuf_deinit(str_sbuf_t *sp);
void str_sbuf_insert(str_sbuf_t *sp, char *item);
char *str_sbuf_remove(str_sbuf_t *sp);

#endif /* __SBUF_H__ */
