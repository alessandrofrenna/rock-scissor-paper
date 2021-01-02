#include <inttypes.h>
#include <netinet/in.h>

typedef struct tcp_client_info_t
{
    int sock_descriptor;
    struct sockaddr_in sock_addr;
} tcp_client_info;

typedef struct player_info_t {
    char nickname[10];
    uint32_t ip_addr;
    uint16_t udp_port;
    tcp_client_info *client_info;
} player_info;