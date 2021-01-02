#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "queue.h"

queue *init_queue() {
    pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t are_even = PTHREAD_COND_INITIALIZER;
    queue *q = (queue *)malloc(sizeof(queue));
    q->head = q->tail = NULL;
    q->lock = queue_lock;
    q->are_even = are_even;
    return q;
}

void enqueue (queue *q, player_info val) {
    queue_item *item = (queue_item *)malloc(sizeof(queue_item));
    item->value = val;
    item->next = NULL;

    if (!q->tail) {
        q->head = q->tail = item;
        return;
    }

    q->tail->next = item;
    q->tail = item;
    return;
}

queue_item *dequeue (queue *q) {
    if (!q->head) {
        return NULL;
    }

    queue_item *temp = q->head;
    q->head = q->head->next;
    
    if (!q->head) {
        q->tail = NULL;
    }
    
    return temp;
}

void clear_queue(queue *q) {
    while (q->head) {
        dequeue(q);
    }
}
