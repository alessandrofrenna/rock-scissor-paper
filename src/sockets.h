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

typedef struct tcp_client_info_t
{
    int sock_descriptor;
    struct sockaddr_in sock_addr;
} tcp_client_info;

int is_little_endian();

int connect_to_tcp_client(struct sockaddr_in);
int connect_to_udp_client();
int make_udp_server();
int make_tcp_server();

unsigned char *serialize_tcp_request(unsigned char *, tcp_client_request);
unsigned char *serialize_tcp_response(unsigned char *, tcp_server_response);
tcp_client_request *deserialize_tcp_request(unsigned char *);
tcp_server_response *deserialize_tcp_response(unsigned char *);

unsigned char *serialize_char (unsigned char *, char);
unsigned char *serialize_uint16_t (unsigned char *, uint16_t);
unsigned char *serialize_uint32_t (unsigned char *, uint32_t);
unsigned char *serialize_nickname (unsigned char*, char *);

unsigned char *deserialize_char (unsigned char *, char *);
unsigned char *deserialize_uint16_t (unsigned char *, uint16_t *);
unsigned char *deserialize_uint32_t (unsigned char *, uint32_t *);
unsigned char *deserialize_nickname (unsigned char*, char *);
