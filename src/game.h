#include "sockets.h"

#define WINNING_SCORE 5

typedef enum states {
    MAKING_CHOICE,
    WAITING,
    ELABORATING,
    FINISH
} game_states;

typedef struct player {
    char nickname[NICKNAME_LEN];
    uint8_t points;
    uint8_t last_choice;
} player;

typedef struct game {
    player *player1;
    player *player2;
    char winner[NICKNAME_LEN];
} game;

typedef struct packet
{
    char type;
    uint8_t choice;
    uint8_t winner;
    uint8_t loser;
} game_packet;

void game_setup(char *, struct sockaddr_in);
void run_game_loop();
void *receive_packets(void *);
void *handle_packets_content(void *);
void send_player_choice(char);
void send_can_continue_play();
void send_game_finished();