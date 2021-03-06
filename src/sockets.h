#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <time.h>

#define TCP_PORT 3000
#define TCP_REQ_LEN 1024
#define TCP_RES_LEN 16
#define NICKNAME_LEN 10
#define HTTP_HEADER_LEN 512
#define HTTP_MESSAGE_LEN 65535

typedef struct tcp_req_t
{
    char nickname[NICKNAME_LEN];
    uint16_t udp_port;
} tcp_client_request;

typedef struct tcp_res_t
{
    char nickname[NICKNAME_LEN];
    uint32_t ip_address;
    uint16_t udp_port;
} tcp_server_response;

int connect_to_tcp_server(struct sockaddr_in addr)
{
    int descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (descriptor < 0)
    {
        perror("TCP connection - error");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    if (connect(descriptor, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("TCP connection - connecting error");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    return descriptor;
}

int make_udp_server()
{
    struct sockaddr_in address;
    int descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (descriptor < 0)
    {
        perror("UDP server - error");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    // Popoliamo il campo dell'indirizzo e facciamo il binding
    address.sin_port = 0;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(descriptor, (const struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("UDP server - binding error");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    return descriptor;
}

int make_tcp_server()
{
    int opt = 1;
    struct sockaddr_in address;
    int descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (descriptor < 0)
    {
        perror("TCP server - error creating server");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    // Popoliamo il campo dell'indirizzo e facciamo il binding
    address.sin_family = AF_INET;
    address.sin_port = htons(TCP_PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    // Aggiungiamo l'opzione per riutilizzare l'indirizzo subito dopo disconnessione
    if (setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    {
        perror("TCP server - error setting reuseaddr option");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    // Facciamo il binding tra socket e indirizzo specificato
    if (bind(descriptor, (const struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("TCP server - binding failed");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }

    listen(descriptor, 10);

    printf("TCP server - listening on port %d\n", ntohs(address.sin_port));

    return descriptor;
}

int is_little_endian()
{
    int n = 1;
    if (*(char *)&n == 1)
        return 1;
    return 0;
}

unsigned char *serialize_uint16_t(unsigned char *buffer, uint16_t value)
{
    if (is_little_endian())
    {
        buffer[0] = value;
        buffer[1] = (value >> 8);
    }
    else
    {
        buffer[0] = (value >> 8);
        buffer[1] = value;
    }
    return buffer + 2;
}

unsigned char *serialize_uint32_t(unsigned char *buffer, uint32_t value)
{
    if (is_little_endian())
    {
        buffer[0] = value;
        buffer[1] = value >> 8;
        buffer[2] = value >> 16;
        buffer[3] = value >> 24;
    }
    else
    {
        buffer[0] = value >> 24;
        buffer[1] = value >> 16;
        buffer[2] = value >> 8;
        buffer[3] = value;
    }

    return buffer + 4;
}

unsigned char *serialize_nickname (unsigned char* buffer, char *nickname)
{
    memmove(buffer, nickname, NICKNAME_LEN);
    return buffer + NICKNAME_LEN;
}

unsigned char *deserialize_uint16_t(unsigned char *buffer, uint16_t *num)
{
    *num = *((uint16_t *) buffer);
    *num = ntohs(*num);
    return buffer + 2;
}

unsigned char *deserialize_uint32_t(unsigned char *buffer, uint32_t *num)
{
    *num = *((uint32_t *) buffer);
    *num = ntohl(*num);
    return buffer + 4;
}

unsigned char *deserialize_nickname (unsigned char* buffer, char *nickname)
{
    memmove(nickname, buffer, 10);
    return buffer + 10;
}

unsigned char *serialize_tcp_request(unsigned char *buffer, tcp_client_request req)
{
    buffer = serialize_nickname(buffer, req.nickname);
    buffer = serialize_uint16_t(buffer, req.udp_port);
    return buffer;
}

unsigned char *serialize_tcp_response(unsigned char *buffer, tcp_server_response res)
{
    buffer = serialize_nickname(buffer, res.nickname);
    buffer = serialize_uint32_t(buffer, res.ip_address);
    buffer = serialize_uint16_t(buffer, res.udp_port);
    return buffer;
}

tcp_client_request *deserialize_tcp_request(unsigned char * buffer)
{   
    tcp_client_request *req = (tcp_client_request *)malloc(sizeof(tcp_client_request));
    buffer = deserialize_nickname(buffer, req->nickname);
    buffer = deserialize_uint16_t(buffer, &req->udp_port);
    return req;
}

tcp_server_response *deserialize_tcp_response(unsigned char * buffer)
{
    tcp_server_response *res = (tcp_server_response *)malloc(sizeof(tcp_server_response));
    buffer = deserialize_nickname(buffer, res->nickname);
    buffer = deserialize_uint32_t(buffer, &res->ip_address);
    buffer = deserialize_uint16_t(buffer, &res->udp_port);
    return res;
}
