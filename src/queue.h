#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_QUEUE_SIZE 1024

typedef struct player_info_t {
    int db_index;
    unsigned int descr_index;
    char nickname[10];
    uint32_t ip_addr;
    uint16_t udp_port;
} player_info;

typedef struct queue_item_t {
    player_info value;
    struct queue_item_t *next;
} queue_item;

typedef struct queue_t {
    unsigned int item_count;
    queue_item *head;
    queue_item *tail;
    pthread_mutex_t mutex;
    pthread_cond_t can_produce_element;
    pthread_cond_t can_consume_element;
} queue;

queue *init_queue() {
    pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    queue *q = (queue *)malloc(sizeof(queue));
    q->head = q->tail = NULL;
    q->mutex = queue_lock;
    q->can_consume_element = q->can_produce_element = cond;
    q->item_count = 0;
    return q;
}

void enqueue(queue *q, player_info val) {
    pthread_mutex_lock(&(q->mutex));

    if (q->item_count >= MAX_QUEUE_SIZE)
    {
        pthread_cond_wait(&(q->can_produce_element), &(q->mutex));
    }

    queue_item *item = (queue_item *)malloc(sizeof(queue_item));
    item->value = val;
    item->next = NULL;

    if (!q->tail) {
        q->head = q->tail = item;
    }
    else 
    {
        q->tail->next = item;
        q->tail = item;
    }

    ++q->item_count;
    pthread_mutex_unlock(&(q->mutex));
    pthread_cond_signal(&(q->can_consume_element));
    return;
}

queue_item *dequeue(queue *q) {
    pthread_mutex_lock(&(q->mutex));
    if (!q->head) {
        pthread_cond_wait(&(q->can_consume_element), &(q->mutex));
    }

    queue_item *temp = q->head;
    q->head = q->head->next;
    
    if (!q->head) {
        q->tail = NULL;
    }
    
    --q->item_count;

    if (q->item_count < MAX_QUEUE_SIZE)
    {
        pthread_mutex_unlock(&(q->mutex));
        pthread_cond_signal(&(q->can_produce_element));
    } else {
        pthread_mutex_unlock(&(q->mutex));
    }
    return temp;
}

void clear_queue(queue *q) {
    while (q->head) {
        dequeue(q);
    }
}