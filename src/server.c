#include <inttypes.h>
#include <pthread.h>
#include <sqlite3.h>
#include "queue.h"
#include "sockets.h"

#define MAX_CONCURRENT_PLAYERS 1024

// Variabili globalix
sqlite3 *db_desc;
unsigned int clients_count = 0;
queue *players_queue = NULL;
int tcp_sock_descriptors[MAX_CONCURRENT_PLAYERS];
struct sockaddr_in tcp_sock_addresses[MAX_CONCURRENT_PLAYERS];
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clients_count_lock = PTHREAD_MUTEX_INITIALIZER;

// Prototipi delle funzioni del server
void *handle_client_requests(void *);
void *ready_players_handler(void *);
void send_tcp_response(unsigned int, tcp_server_response);
void handle_http_get_request(int);
int insert_client_into_db(struct sockaddr_in, char *,  uint16_t);
void update_client_db_info(int, int);
void list_all_clients_from_db(char *);
void ctrl_c_handler();

int main()
{
    unsigned int client_idx = 0;
    // Apriamo la connessione al database SQLite3 e controlliamo eventuali errori
    char db_path[2000];
    getcwd(db_path, sizeof(db_path));
    strcat(db_path, "/");
    strcat(db_path, "clients.db");
    unsigned int ret_code = sqlite3_open(db_path, &db_desc);
    if (ret_code != SQLITE_OK)
    {
        printf("Database server - connection error: %s", sqlite3_errmsg(db_desc));
        exit(EXIT_FAILURE);
    } else {
        puts("Database server - connected successfully");
    }
    // Apriamo la connessione con il server TCP che fungerà anche da server WEB
    int server_descriptor = make_tcp_server();
    players_queue = init_queue(); // Inizializziamo la coda

    // Creiamo il thread che si occuperà di leggere dalla coda
    pthread_t queue_consumer;
    pthread_create(&queue_consumer, NULL, ready_players_handler, NULL);
    pthread_detach(queue_consumer);

    // Loop principale che accetta connessioni
    while (1)
    {
        if (clients_count < MAX_CONCURRENT_PLAYERS)
        {
            // Se siamo arrivati all'ultimo indice valido, azzeriamo client_idx
            if (client_idx >= MAX_CONCURRENT_PLAYERS)
            {
                client_idx = 0;
            }

            // Accettiamo la connessione TCP fatta da un client
            // Acquisiamo il lock per settare il client connesso
            pthread_mutex_lock(&client_lock);
            int addr_len = sizeof(tcp_sock_addresses[client_idx]);
            tcp_sock_descriptors[client_idx] = accept(
                server_descriptor,
                (struct sockaddr *)&tcp_sock_addresses[client_idx],
                &addr_len
            );
            pthread_mutex_unlock(&client_lock);

            if (tcp_sock_descriptors[client_idx] < 0)
            {
                perror("TCP server - error accepting client connection");
                fflush(stderr);
            }

            printf(
                "TCP server: client connected from %s:%d\n",
                inet_ntoa(tcp_sock_addresses[client_idx].sin_addr),
                ntohs(tcp_sock_addresses[client_idx].sin_port)
            );

            // Creiamo un oggetto di tipo player info da far usare al thread
            player_info player;
            player.descr_index = client_idx;
            player.ip_addr = tcp_sock_addresses[client_idx].sin_addr.s_addr;

            // Creiamo un thread detached che gestirà le richieste del client
            pthread_t thread;
            pthread_create(&thread, NULL, handle_client_requests, &player);
            pthread_detach(thread);
            
            // Acquisiamo il lock ed incrementiamo il conto dei client connessi
            pthread_mutex_lock(&clients_count_lock);
            clients_count++;
            pthread_mutex_unlock(&clients_count_lock);
            
            client_idx++;
        }
    }

    sqlite3_close(db_desc);
    close(server_descriptor);
    return 0;
}

void *handle_client_requests(void *ptr)
{
    char buffer[TCP_REQ_LEN];
    player_info player = *((player_info *)ptr);

    // Riceviamo i dati sulla socket del client
    recv(tcp_sock_descriptors[player.descr_index], buffer, TCP_REQ_LEN, 0);

    // Controlliamo se la richiesta è una richiesta HTTP - GET
    if (strncmp("GET", buffer, 3) == 0)
    {
        handle_http_get_request(player.descr_index);
    }
    else
    {
        // Convertiamo il buffer in un oggetto tcp_client_request
        // mettiamo in coda i dati relativi al giocatore che aspetta l'avversario
        tcp_client_request *request = deserialize_tcp_request(buffer);

        // Popoliamo i dati mancanti in player_info con quelli ottenuti dal client
        player.udp_port = request->udp_port;
        strcpy(player.nickname, request->nickname);

        // Inseriamo i dati del client nel database
        player.db_index = insert_client_into_db(tcp_sock_addresses[player.descr_index], player.nickname, player.udp_port);

        // Accesso concorrente alla coda per salvare i dati del giocatore che aspetta
        pthread_mutex_lock(&players_queue->lock);
        enqueue(players_queue, player);
        if (clients_count > 0 && clients_count % 2 == 0)
        {
            pthread_cond_signal(&players_queue->are_even);
        }
        pthread_mutex_unlock(&players_queue->lock);
    }

    pthread_exit(NULL);
}

void *ready_players_handler(void *ptr)
{
    while (1)
    {
        pthread_mutex_lock(&players_queue->lock);

        if (clients_count <= 0 || clients_count % 2 != 0)
        {
            pthread_cond_wait(&players_queue->are_even, &players_queue->lock);
        }

        tcp_server_response p1_res, p2_res;
        player_info p1 = dequeue(players_queue)->value;
        player_info p2 = dequeue(players_queue)->value;

        // Popoliamo l'oggetto relativo alla risposta che verrà mandata a giocatore #1
        p1_res.ip_address = htonl(p2.ip_addr);
        p1_res.udp_port = htons(p2.udp_port);
        strcpy(p1_res.nickname, p2.nickname);

        // Popoliamo l'oggetto relativo alla risposta che verrà mandata a giocatore #2
        p2_res.ip_address = htonl(p1.ip_addr);
        p2_res.udp_port = htons(p1.udp_port);
        strcpy(p2_res.nickname, p1.nickname);

        // Mandiamo a giocatore #1 i dati di giocatore #2 e viceversa
        send_tcp_response(p1.descr_index, p1_res);
        send_tcp_response(p2.descr_index, p2_res);

        // Se siamo qui, aggiorniamo i dati sul database
        update_client_db_info(p1.db_index, p2.db_index);
        update_client_db_info(p2.db_index, p1.db_index);

        pthread_mutex_lock(&client_lock);
        close(tcp_sock_descriptors[p1.descr_index]);
        close(tcp_sock_descriptors[p2.descr_index]);
        pthread_mutex_unlock(&client_lock);

        pthread_mutex_lock(&clients_count_lock);
        clients_count -= 2;
        pthread_mutex_unlock(&clients_count_lock);

        // Rilasciamo il lock del mutex
        pthread_mutex_unlock(&players_queue->lock);
    }
}

void send_tcp_response(unsigned int descriptor_index, tcp_server_response response)
{
    unsigned int bytes_sent = 0;
    char buffer[TCP_RES_LEN], *buff_ptr;

    buff_ptr = serialize_tcp_response(buffer, response);

    while (bytes_sent < TCP_RES_LEN)
    {
        bytes_sent += send(tcp_sock_descriptors[descriptor_index], buffer, buff_ptr - buffer, 0);
    }
}

void handle_http_get_request(int desc_idx)
{
    unsigned int bytes_sent = 0;
    char res_head[HTTP_HEADER_LEN], res_msg[HTTP_MESSAGE_LEN];

    sprintf(res_msg, 
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Rock - Paper - Scissor</title>" \
        "<style>html, .body { font-family: arial, sans-serif; } .container { display: flex; flex-direction: column }"\
        "table { border-collapse: collapse; width: 70%; } td, th { border: 1px solid #dddddd; text-align: left; padding: 8px; }"\
        "tr:nth-child(even) { background-color: #dddddd; } </style> </head> <body> <div class=\"container\"> <div class=\"item\">"\
        "<h3>Rock - Paper - Scissor clients history</h3> </div> <div class=\"item\"><table><thead><tr>"\
        "<th>Player nickname</th><th>Player IP address</th><th>Player TCP port</th><th>Player UDP port</th>"\
        "<th>Opponent nickname</th><th>Opponent IP address</th><th>Opponent TCP port</th><th>Opponent UDP port</th><th>Updated at</th></tr></thead><tbody>"
    );
    list_all_clients_from_db(res_msg);
    sprintf(&res_msg[strlen(res_msg)], "</tbody></table></div></div></body></html>");

    sprintf(res_head, "HTTP/1.1 200 OK\r\n");
    sprintf(&res_head[strlen(res_head)], "Content-Type: text/html; charset=utf-8\r\n");
    sprintf(&res_head[strlen(res_head)], "Content-Length: %d\r\n", strlen(res_msg));
    sprintf(&res_head[strlen(res_head)], "\r\n");

    send(tcp_sock_descriptors[desc_idx], res_head, strlen(res_head), 0);
    send(tcp_sock_descriptors[desc_idx], res_msg, strlen(res_msg), 0);

    // Creaiamo il response body con i dati presi dal database

    pthread_mutex_lock(&client_lock);
    close(tcp_sock_descriptors[desc_idx]);
    pthread_mutex_unlock(&client_lock);

    pthread_mutex_lock(&clients_count_lock);
    clients_count--;
    pthread_mutex_unlock(&clients_count_lock);

    pthread_exit(NULL);
}

int insert_client_into_db(struct sockaddr_in tcp_addr, char *nickname,  uint16_t udp_port)
{
    sqlite3_stmt *stmt;
    char *query = "INSERT INTO clients (IP, TCP_PORT, UDP_PORT, NICKNAME, UPDATED_AT)" \
                  "VALUES (?1, ?2, ?3, ?4, strftime('%s','now'))";
    sqlite3_prepare_v2(db_desc, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, inet_ntoa(tcp_addr.sin_addr), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, ntohs(tcp_addr.sin_port));
    sqlite3_bind_int(stmt, 3, udp_port);
    sqlite3_bind_text(stmt, 4, nickname, -1, SQLITE_STATIC);
    int ret_code = sqlite3_step(stmt);
    
    if (ret_code != SQLITE_DONE)
    {
        printf("Database server - error inserting record: %s\n", sqlite3_errmsg(db_desc));
    }

    int record_id = (int)sqlite3_last_insert_rowid(db_desc);

    sqlite3_finalize(stmt);
    return record_id;
}

void update_client_db_info(int player_id, int opponent_id)
{
    sqlite3_stmt *stmt;
    char *query = "UPDATE clients SET OPPONENT_ID = ?1, UPDATED_AT = strftime('%s','now') WHERE ID = ?2";
    sqlite3_prepare_v2(db_desc, query, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, opponent_id);
    sqlite3_bind_int(stmt, 2, player_id);
    int ret_code = sqlite3_step(stmt);
    
    if (ret_code != SQLITE_DONE)
    {
        printf("Database server - error inserting record: %s\n", sqlite3_errmsg(db_desc));
    }

    sqlite3_finalize(stmt);
}

void list_all_clients_from_db(char *buffer)
{
    int ret_code;
    sqlite3_stmt *stmt;
    char *query = "SELECT " \
                  " one.NICKNAME, one.IP, one.TCP_PORT, one.UDP_PORT, " \
                  " two.NICKNAME, two.IP, two.TCP_PORT, two.UDP_PORT, datetime(two.UPDATED_AT,'unixepoch') AS UPDATED_AT" \
                  " FROM clients as one " \
                  " INNER JOIN clients as two " \
                  " ON one.ID = two.OPPONENT_ID" \
                  " ORDER BY UPDATED_AT DESC";

    sqlite3_prepare_v2(db_desc, query, -1, &stmt, NULL);

    while ((ret_code = sqlite3_step(stmt)) == SQLITE_ROW) 
    {
        sprintf(&buffer[strlen(buffer)], "<tr>");
        sprintf(&buffer[strlen(buffer)], "<td>%s</td>", sqlite3_column_text(stmt, 0));
        sprintf(&buffer[strlen(buffer)], "<td>%s</td>", sqlite3_column_text(stmt, 1));
        sprintf(&buffer[strlen(buffer)], "<td>%d</td>", sqlite3_column_int(stmt, 2));
        sprintf(&buffer[strlen(buffer)], "<td>%d</td>", sqlite3_column_int(stmt, 3));
        sprintf(&buffer[strlen(buffer)], "<td>%s</td>", sqlite3_column_text(stmt, 4));
        sprintf(&buffer[strlen(buffer)], "<td>%s</td>", sqlite3_column_text(stmt, 5));
        sprintf(&buffer[strlen(buffer)], "<td>%d</td>", sqlite3_column_int(stmt, 6));
        sprintf(&buffer[strlen(buffer)], "<td>%d</td>", sqlite3_column_int(stmt, 7));
        sprintf(&buffer[strlen(buffer)], "<td>%s</td>", sqlite3_column_text(stmt, 8));
        sprintf(&buffer[strlen(buffer)], "</tr>");
    }

    sqlite3_finalize(stmt);
}

void ctrl_c_handler()
{
    sqlite3_close(db_desc);
    clear_queue(players_queue);
    exit(EXIT_SUCCESS);
}