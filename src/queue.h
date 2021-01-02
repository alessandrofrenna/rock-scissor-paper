#include <inttypes.h>
#include <pthread.h>

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
    queue_item *head;
    queue_item *tail;
    pthread_mutex_t lock;
    pthread_cond_t are_even;
} queue;

queue *init_queue();
void enqueue (queue *, player_info);
queue_item *dequeue (queue *);
void clear_queue(queue *);