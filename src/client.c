#include <argp.h>
#include "game.h"

typedef struct options
{
    char nickname[10];
    uint32_t server_address;
    uint16_t server_port;
} client_options;

// Prototipi di funzioni
static error_t parse_argument(int, char *, struct argp_state *);

int main(int argc, char *argv[])
{
    /**
     * In questa sezione del game client ci occupiamo di acquisire
     * gli argomenti attraverso argc e argv mediante l'uso delle
     * funzioni contenute nell'intestazione "argp.h"
     */
    client_options arguments;
    const char *argp_program_version = "Rock Scissor Paper 1.0";
    const char *argp_program_bug_address = "Alessandro Frenna <alessandrofrenna95@gmail.com>";
    static char doc[] = "Game client for a client/server rock scissor paper game for university project";
    static char arg_doc[] = "";

    static const struct argp_option options[] =
        {
            {"nickname", 'n', "NICKNAME", 0, "Nickname of the player"},
            {"server-address", 'a', "SERVER ADDRESS", 0, "Address of the game server"},
            {"server-port", 'p', "SERVER PORT", 0, "Port of the game server"},
            {0}};

    struct argp arg_parser = {options, parse_argument, arg_doc, doc};
    argp_parse(&arg_parser, argc, argv, 0, 0, &arguments);
    
    // Inizializziamo il gioco
    struct sockaddr_in tcp_server_address;
    tcp_server_address.sin_family = AF_INET;
    tcp_server_address.sin_port = arguments.server_port;
    tcp_server_address.sin_addr.s_addr = arguments.server_address;
    game_setup(arguments.nickname, tcp_server_address);
    return 0;
}

static error_t parse_argument(int key, char *arg, struct argp_state *state)
{
    client_options *options = state->input;
    switch (key) {
        case 'a':
        {
            options->server_address = inet_addr(arg);
            break;
        }
        case 'n':
        {
            if (strlen(arg) > 9) 
            {
                perror("Nickname length must be less than 10 characters");
                fflush(stderr);
                exit(EXIT_FAILURE);
            }
            strcpy(options->nickname, arg);
            break;
        }
        case 'p':
        {
            options->server_port = htons(atoi(arg));
            break;
        }
        case ARGP_KEY_ARG:
        {
            if (state->argc > 4)
            {
                /* Too many arguments. */
                argp_usage(state);
            }
            break;
        }
        case ARGP_KEY_END:
        {
            if (state->argc < 4)
            {
                /* Not enough arguments. */
                argp_usage(state);
            }
            break;
        }
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}