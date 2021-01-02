#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include "game.h"

int match_num = 1;
game *rps_game;
game_states state;

int udp_player_server_sock = 1;
struct sockaddr_in udp_player_sock_addr;
struct sockaddr_in udp_opponent_sock_addr;

int is_full_buffer = 0;
game_packet received_packet;
pthread_mutex_t packet_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t is_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t is_full = PTHREAD_COND_INITIALIZER;

void game_setup(char *player_nickname, struct sockaddr_in tcp_server_address)
{
    socklen_t addrlen = sizeof(udp_player_sock_addr);
    int tcp_player_client_sock = 0;
    int bytes_sent = 0, bytes_received = 0;
    unsigned char req_buffer[TCP_REQ_LEN], res_buffer[TCP_RES_LEN];

    // Allochiamo la memoria necessaria per le strutture
    // che rappresentano i due giocatori della partita
    // ed allochiamo anche la memoria necessaria all'oggetto
    // che conterrà le informazioni sullo stato della partita
    rps_game = (game *)malloc(sizeof(game));
    player *player1 = (player *)malloc(sizeof(player));
    player *player2 = (player *)malloc(sizeof(player));

    // Assegnamo i due giocatori all'oggetto rps_game
    rps_game->player1 = player1;
    rps_game->player2 = player2;

    // Inizializziamo i dati della struttura del giocatore 1
    player1->points = 0;
    player1->last_choice = 0;
    strcpy(player1->nickname, player_nickname);

    // Creaiamo il server UDP e otteniamo
    // i dati sull'indirizzo associato alla socket
    udp_player_server_sock = make_udp_server();
    if (getsockname(udp_player_server_sock, (struct sockaddr *)&udp_player_sock_addr, &addrlen) < 0)
    {
        perror("UDP server - error getting socketname");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    // Creaiamo il collegamento con il server TCP
    tcp_player_client_sock = connect_to_tcp_client(tcp_server_address);

    // Serializziamo la richiesta TCP da mandare
    tcp_client_request request = {.udp_port = udp_player_sock_addr.sin_port};
    strcpy(request.nickname, player_nickname);
    serialize_tcp_request(req_buffer, request);

    // Mandiamo la richiesta al server TCP per mettere il giocatore in coda
    while (bytes_sent < TCP_REQ_LEN)
    {
        bytes_sent += send(tcp_player_client_sock, req_buffer, TCP_REQ_LEN, 0);
    }

    // Rimaniamo in ascolto finché non otteniamo i dati dell'avversario
    while (bytes_received < TCP_RES_LEN)
    {
        if ((bytes_received += recv(tcp_player_client_sock, res_buffer, TCP_RES_LEN, 0)) > 0)
        {
            tcp_server_response *response = deserialize_tcp_response(res_buffer);
            udp_opponent_sock_addr.sin_family = AF_INET;
            udp_opponent_sock_addr.sin_port = htons(response->udp_port);
            udp_opponent_sock_addr.sin_addr.s_addr = response->ip_address;
            strcpy(player2->nickname, response->nickname);
        }
    }

    close(tcp_player_client_sock);

    // Thread per gestire i dati che arrivano dall'avversario
    // il meccanismo usato è quello produttore/consumatore
    pthread_t packets_handler, packets_receiver;

    // Creaiamo il thread che permette di gestire i pacchetti in entrata
    pthread_create(&packets_handler, NULL, handle_packets_content, NULL);
    pthread_detach(packets_handler);

    // Creiamo il thread che permette di ricevere pacchetti in entrata
    pthread_create(&packets_receiver, NULL, receive_packets, NULL);
    pthread_detach(packets_receiver);

    run_game_loop();
    return;
}

void run_game_loop()
{
    char choice;
    int is_ended = 0;
    uint8_t p1_choice;
    uint8_t p2_choice;
    uint8_t match_winner;
    uint8_t score_lut[3][3] = {{0, 1, 2}, {2, 0, 1}, {1, 2, 0}};

    pthread_mutex_lock(&state_lock);
    state = MAKING_CHOICE;
    pthread_mutex_unlock(&state_lock);

    while (!is_ended)
    {
        switch (state)
        {
            case MAKING_CHOICE:
                printf("\n*********************************\n");
                printf("*            Match %d           *\n", match_num);
                printf("*********************************\n");

                do
                {
                    printf("Make a choice: (R)ock, (S)cissor, (P)aper > ");
                    fflush(stdout);
                    scanf(" %c", &choice);
                    choice = tolower(choice);
                    fflush(stdout);
                } while (choice != 'r' && choice != 's' && choice != 'p');

                send_player_choice(choice);
                break;
            case ELABORATING:
                // Indici nella lookup table zero based, quindi dobbiamo
                // decrementare di una unità le scelte dei giocatori prima
                // di ottenere il vincitore di un match attraverso lut
                p1_choice = rps_game->player1->last_choice - 1;
                p2_choice = rps_game->player2->last_choice - 1;
                match_winner = score_lut[p1_choice][p2_choice];

                printf("\nYou choosed: %s\n", p1_choice == 0 ? "rock" : p1_choice == 1 ? "scissor" : "paper");
                printf("Opponent, %s choosed: %s\n", rps_game->player2->nickname, p2_choice == 0 ? "rock" : p2_choice == 1 ? "scissor" : "paper");

                if (match_winner == 1)
                {
                    rps_game->player1->points += 1;
                    printf("You won the match!\n");
                }
                else if (match_winner == 2)
                {
                    rps_game->player2->points += 1;
                    printf("You lose! %s won!\n", rps_game->player2->nickname);
                }
                else
                {
                    printf("Tie\n");
                }

                if (rps_game->player1->points != WINNING_SCORE && rps_game->player2->points != WINNING_SCORE)
                {
                    send_can_continue_play();
                }
                else
                {
                    send_game_finished();
                }

                break;
            case FINISH:
                system("clear");

                if (received_packet.winner == 1)
                {
                    printf("You won!\n");
                }
                else
                {
                    printf("Game Over\nYou lose! %s won!\n", rps_game->player2->nickname);
                }

                printf("Final score:\n");
                printf("%s - %d\n", rps_game->player1->nickname, rps_game->player1->points);
                printf("%s - %d\n", rps_game->player2->nickname, rps_game->player2->points);

                is_ended = 1;
                break;
            default:
                break;
        }
    }
    return;
}

void *receive_packets(void *ptr)
{
    socklen_t addrlen = 10;
    struct sockaddr_in opponent_addr;

    while (1)
    {
        game_packet *pkt = (game_packet *)malloc(sizeof(game_packet));

        int bytes_recvd = recvfrom(
            udp_player_server_sock,
            pkt,
            sizeof(*pkt),
            0,
            (struct sockaddr *)&opponent_addr,
            &addrlen);

        pthread_mutex_lock(&packet_lock);

        if (is_full_buffer)
        {
            pthread_cond_wait(&is_empty, &packet_lock);
        }

        received_packet = *pkt;
        is_full_buffer = 1;
        free(pkt);
        pthread_cond_signal(&is_full);
        pthread_mutex_unlock(&packet_lock);
    }
}

void *handle_packets_content(void *ptr)
{
    while (1)
    {
        pthread_mutex_lock(&packet_lock);
        if (!is_full_buffer)
        {
            pthread_cond_wait(&is_full, &packet_lock);
        }

        if (state == WAITING)
        {
            switch (received_packet.type)
            {
                case 'c':
                    rps_game->player2->last_choice = received_packet.choice;
                    pthread_mutex_lock(&state_lock);
                    state = ELABORATING;
                    pthread_mutex_unlock(&state_lock);
                    break;
                case 's':
                    match_num++;
                    pthread_mutex_lock(&state_lock);
                    state = MAKING_CHOICE;
                    pthread_mutex_unlock(&state_lock);
                    break;
                case 'f':
                    pthread_mutex_lock(&state_lock);
                    state = FINISH;
                    pthread_mutex_unlock(&state_lock);
                    break;
                default:
                    break;
            }

            is_full_buffer = 0;

            pthread_cond_signal(&is_empty);
        }

        pthread_mutex_unlock(&packet_lock);
    }
}

void send_player_choice(char choice)
{
    // Creaiamo il pacchetto che ha per tipo - type - c - choice
    game_packet pkt;
    rps_game->player1->last_choice = choice == 'r' ? 1 : choice == 's' ? 2 : 3;
    pkt.type = 'c';
    pkt.loser = pkt.winner = 0;
    pkt.choice = rps_game->player1->last_choice;

    int bytes_sent = sendto(
        udp_player_server_sock,
        (game_packet *)&pkt,
        sizeof(pkt),
        0,
        (struct sockaddr *)&udp_opponent_sock_addr,
        sizeof(struct sockaddr_in));

    if (bytes_sent > 0)
    {
        pthread_mutex_lock(&state_lock);
        state = WAITING;
        pthread_mutex_unlock(&state_lock);
    }
}

void send_can_continue_play()
{
    // Creaiamo il pacchetto che ha per tipo - type - s - signal/status
    game_packet pkt;
    pkt.type = 's';
    pkt.choice = pkt.loser = pkt.winner = 0;

    int bytes_sent = sendto(
        udp_player_server_sock,
        (game_packet *)&pkt,
        sizeof(pkt),
        0,
        (struct sockaddr *)&udp_opponent_sock_addr,
        sizeof(struct sockaddr_in));

    if (bytes_sent > 0)
    {
        pthread_mutex_lock(&state_lock);
        state = WAITING;
        pthread_mutex_unlock(&state_lock);
    }
}

void send_game_finished()
{
    // Creaiamo il pacchetto che ha per tipo - type - f - finish
    game_packet pkt;
    pkt.type = 'f';
    pkt.choice = 0;

    // Quando dobbiamo determinare il vincitore ed il perdente
    // player1 e player2 saranno invertiti dall'altra parte della
    // socket UDP: se io sono player1 ed ho vinto, il vincitore per
    // il mio avversario sarà player2. Il perdente che per me è
    // il mio avveersario, player2, sarà per il mio avveresario player1
    // e viceversa
    if (rps_game->player1->points >= WINNING_SCORE)
    {
        pkt.winner = 2;
        pkt.loser = 1;
    }
    else
    {
        pkt.winner = 1;
        pkt.loser = 2;
    }

    int bytes_sent = sendto(
        udp_player_server_sock,
        (game_packet *)&pkt,
        sizeof(pkt),
        0,
        (struct sockaddr *)&udp_opponent_sock_addr,
        sizeof(struct sockaddr_in));

    if (bytes_sent > 0)
    {
        pthread_mutex_lock(&state_lock);
        state = WAITING;
        pthread_mutex_unlock(&state_lock);
    }
}