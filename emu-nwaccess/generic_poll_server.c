
#include <stdbool.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>


#ifdef SKARSNIK_DEBUG
#define s_debug(...)  printf(__VA_ARGS__)
#else
#define s_debug(...)
#endif

#include <stdarg.h>

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <process.h>
#define close(h) if(h){closesocket(h);}
#define write(a,b,c) send(a, b, c, 0)
#define poll(a, b, c) WSAPoll(a, b, c);
typedef LPWSAPOLLFD poll_fd_t;
#define socklen_t int
#else
#include <poll.h>
    typedef int SOCKET;
    typedef struct pollfd poll_fd_t;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#define ioctlsocket ioctl
#endif

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#ifdef _WIN32

/* On windows, recv return an error when a non blocking socket
*  has nothing left to read. This is wrapper to return 0 instead.
*/
static inline int	read(SOCKET s, char* b, int size)
{
    int res = recv(s, b, size, 0);
    if (res == SOCKET_ERROR)
    {
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            return 0;
        return -1;
    }
    return res;
}
#endif

static void print_socket_error(const char* msg)
{
#ifdef _WIN32
    wchar_t *s = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, WSAGetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&s, 0, NULL);
    fprintf(stderr, "%s - WSA error : %d -- %S\n", msg, WSAGetLastError(), s);
    LocalFree(s);
#else
    fprintf(stderr, "%s : %s\n", msg, strerror(errno));
#endif
}


#include "generic_poll_server.h"

static bool __send_hash_reply(SOCKET socket, bool end, unsigned int key_count, va_list list)
{
    bool toret = true;
    for (unsigned int i = 0; i < key_count; i++)
    {
        char* key = va_arg(list, char*);
        char* value = va_arg(list, char*);
        unsigned int size_kv = strlen(key) + strlen(value);
        char* buffer = (char*) malloc(size_kv + 3);
        snprintf(buffer, size_kv + 2, "%s:%s", key, value);
        buffer[size_kv + 1] = '\n';
        if (write(socket, buffer, size_kv + 2) == -1)
        {
            free(buffer);
            generic_poll_server_set_client_state_error(socket);
            return false;
        }
        buffer[size_kv + 2] = 0;
        s_debug("Sending : %s", buffer);
        free(buffer);
        //dprintf(socket, "%s=%s\n", key, value); This does not exists on windows
    }
    if (end)
    {
        return generic_poll_server_end_hash_reply(socket);
    }
    return true;
}

inline bool    generic_poll_server_start_hash_reply(SOCKET socket)
{
    if (write(socket, "\n", 1) == -1)
    {
        generic_poll_server_set_client_state_error(socket);
        return false;
    }
    return true;
}


inline bool    generic_poll_server_end_hash_reply(SOCKET socket)
{
    if (write(socket, "\n", 1) == -1)
    {
        generic_poll_server_set_client_state_error(socket);
        return false;
    }
    return true;
}

bool    generic_poll_server_send_hash_reply(SOCKET socket, int key_count, ...)
{
    va_list list;
    va_start(list, key_count);

    bool toret = __send_hash_reply(socket, false, key_count, list);
    va_end(list);
    return toret;
}

bool    generic_poll_server_send_full_hash_reply(SOCKET socket, int key_count, ...)
{
    va_list list;
    va_start(list, key_count);
    bool toret = generic_poll_server_start_hash_reply(socket);
    if (toret == true)
        toret = __send_hash_reply(socket, true, key_count, list);
    va_end(list);
    s_debug("Full Hash Reply : %d \n", toret);
    return toret;
}

bool generic_poll_server_send_binary_block(SOCKET socket, uint32_t size, const char* data)
{
    if (write(socket, "\0", 1) == -1)
    {
        generic_poll_server_set_client_state_error(socket);
        return false;
    }
    uint32_t network_size = htonl(size);
    if (write(socket, (const char*)&network_size, 4) == -1)
    {
        generic_poll_server_set_client_state_error(socket);
        return false;
    }
    if (write(socket, data, size) == -1)
    {
        generic_poll_server_set_client_state_error(socket);
        return false;
    }
    return true;
}

size_t          generic_poll_server_get_offset(const char *offset_str)
{
    if (offset_str[0] == '$')
    {
        size_t offset = 0;
        sscanf(offset_str + 1, "%zX", &offset);
        return offset;
    }
    return atoll(offset_str);
}

bool        generic_poll_server_send_binary_header(SOCKET socket, uint32_t size)
{
    char header[5];
    header[0] = 0;
    uint32_t network_size = htonl(size);
    memcpy(header + 1, (void*)&network_size, 4);
    if (write(socket, header, 5) == -1)
    {
        generic_poll_server_set_client_state_error(socket);
        return false;
    }
    return true;
}

generic_poll_server_memory_argument*    generic_poll_server_parse_memory_argument(char** ag, unsigned int ac)
{
    generic_poll_server_memory_argument* toret = (generic_poll_server_memory_argument*) malloc(sizeof(generic_poll_server_memory_argument));
    toret->next = NULL;
    toret->offset = 0;
    toret->size = 0;
    generic_poll_server_memory_argument* cur = toret;
    for (unsigned int i = 0; i < ac; i++)
    {
        if (i % 2 == 0)
        {
            cur->offset = generic_poll_server_get_offset(ag[i]);
        } else {
            cur->size = generic_poll_server_get_offset(ag[i]);
            if (i + 1 != ac)
            {
                cur->next = (generic_poll_server_memory_argument*)malloc(sizeof(generic_poll_server_memory_argument));
                cur->next->offset = 0;
                cur->next->size = 0;
                cur->next->next = NULL;
                cur = cur->next;
            }
        }
    }
    return toret;
}

void    generic_poll_server_free_memory_argument(generic_poll_server_memory_argument* tofree)
{
    if (tofree == NULL)
        return;
    generic_poll_server_memory_argument* next = NULL;
    generic_poll_server_memory_argument* cur = tofree->next;
    while (cur != NULL)
    {
        next = cur->next;
        free(cur);
        cur = next;
    }
    free(tofree);
}



static generic_poll_server_client clients[5];
static generic_poll_server_callbacks callbacks = {NULL, NULL, NULL, NULL};


void generic_poll_server_add_callback(generic_poll_server_callback cb, void* fntptr)
{
    switch (cb)
    {
        case SERVER_STARTED:
        {
            callbacks.server_started = (bool(*)(int))fntptr;
            break;
        }
        case NEW_CLIENT:
        {
            callbacks.add_client = (bool(*)(SOCKET))fntptr;
            break;
        }
        case REMOVED_CLIENT:
        {
            callbacks.remove_client = (bool(*)(SOCKET))fntptr;
            break;
        }
        case AFTER_POLL:
        {
            callbacks.after_poll = (bool(*)())fntptr;
            break;
        }
    }
}

static generic_poll_server_client* get_client(SOCKET client_fd)
{
    for (unsigned int i = 0; i < 5; i++)
    {
        if (clients[i].socket_fd == client_fd)
            return &clients[i];
    }
    return NULL;
}

static void remove_client(SOCKET client_fd)
{
    for (unsigned int i = 0; i < 5; i++)
    {
        if (clients[i].socket_fd == client_fd)
        {
            s_debug("Removing client %d\n", client_fd);
            if (clients[i].binary_block != NULL)
                free(clients[i].binary_block);
            clients[i].binary_block = NULL;
            clients[i].socket_fd = 0;
            if (callbacks.remove_client != NULL)
                callbacks.remove_client(client_fd);
            return;
        }
    }
}

void        generic_poll_server_set_client_state_error(SOCKET socket)
{
    generic_poll_server_client* client = get_client(socket);
    client->state = WRITE_ERROR;
}

static bool	add_to_clients(SOCKET client_fd)
{
    for (unsigned int i = 0; i < 5; i++)
    {
        if (clients[i].socket_fd == 0)
        {
            s_debug("Adding new client %d\n", client_fd);
            clients[i].socket_fd = client_fd;
            clients[i].command_data[0] = 0;
            clients[i].command_data_size = 0;
            clients[i].command_data_pos = 0;
            clients[i].state = WAITING_FOR_COMMAND;
            clients[i].binary_block_size = 0;
            clients[i].binary_block = NULL;
            clients[i].binary_header_size = 0;
            clients[i].shallow_binary_block = false;
            return true;
        }
    }
    return false;
}

static bool send_error(SOCKET fd, emulator_network_access_error_type type, const char* error_string)
{
    const char* error_type;
    for (unsigned int i = 0; i < 5; i++)
    {
        if (type == emulator_network_access_error_type_strings[i].error_type)
            error_type = emulator_network_access_error_type_strings[i].string;
    }
    size_t to_send_size = 1 
                          + strlen("error") + 1 + strlen(error_type) + 1
                          + strlen("reason") + 1 + strlen(error_string) + 1
                          + 2;
    char* tosend = (char*) malloc(to_send_size);
    int towrite = snprintf(tosend, to_send_size,
                           "\nerror:%s\nreason:%s\n\n",
                           error_type,
                           error_string);
    s_debug("Sending error :%s\n", tosend);
    if (write(fd, tosend, towrite) == -1)
    {
        free(tosend);
        generic_poll_server_set_client_state_error(fd);
        return false;
    }
    free(tosend);
    return true;
}

static int64_t execute_command(generic_poll_server_client* client, char *cmd_str, char **args, int args_count)
{
    unsigned int command;
    bool    valid_command = false;
    unsigned int custom_command_index = 0;
    for (unsigned int i = 0; i < emulator_network_access_number_of_command; i++)
    {
        if (strcmp(cmd_str, emulator_network_access_command_strings[i].string) == 0)
        {
            command = emulator_network_access_command_strings[i].command;
            valid_command = true;
            break;
        }
    }
    for (unsigned int i = 0; i < custom_emu_nwa_map_size; i++)
    {
        if (strncmp(cmd_str, custom_emu_nwa_map[i].string, strlen(custom_emu_nwa_map[i].string)) == 0)
        {
            command = CUSTOM + i;
            custom_command_index = i;
            valid_command = true;
            break;
        }
    }
    if (!valid_command)
    {
        send_error(client->socket_fd, invalid_command, "Invalid command");
        return 0;
    }
    client->current_command = command;
    if (command >= CUSTOM)
    {
        return custom_emu_nwa_map[custom_command_index].function(client->socket_fd, args, args_count);
    }
    for (unsigned int i = 0; i < generic_emu_mwa_map_size; i++)
    {
        if (generic_emu_mwa_map[i].command == command)
        {
            return generic_emu_mwa_map[i].function(client->socket_fd, args, args_count);
        }
    }
    send_error(client->socket_fd, invalid_command, "Invalid command or not implemented");
    return -1;
}

static void process_binary_block(generic_poll_server_client* client)
{
    if (client->current_command == bCORE_WRITE)
    {
        bool result = generic_poll_server_write_function(client->socket_fd, client->binary_block, client->binary_block_size);
        free(client->binary_block);
        client->binary_block = NULL;
        client->binary_block_size = 0;
        client->binary_header_size = 0;
        client->expected_block_size = 0;
        client->state = WAITING_FOR_COMMAND;
    }
    return;
}

static void process_command(generic_poll_server_client* client)
{
    char* sArgs[7];
    char** args;  
    char *cmd = (char*) malloc(client->command_data_size);
    bool cmd_has_arg = true;
    bool free_args = false;

    //args[0] = NULL;
    char* cmd_block = client->command_data;
    s_debug("Command block is : %d - %s\n", client->command_data_size, cmd_block);

    for (unsigned int i = 0; i < client->command_data_size; i++)
    {
        if (cmd_block[i] == ' ' || cmd_block[i] == '\n')
        {
            memcpy(cmd, cmd_block, i);
            s_debug("i : %d\n", i);
            cmd[i] = 0;
            if (cmd_block[i] == '\n')
                cmd_has_arg = false;
            break;
        }
    }
    s_debug("Cmd : %s\n", cmd);
    cmd_block = cmd_block + strlen(cmd);
    unsigned int nb_arg = 0;
    if (cmd_has_arg)
    {
        unsigned int i = 0;
        // Counting the number of ; to allocate or not memory
        while (cmd_block[i] != '\n')
        {
            if (cmd_block[i] == ';')
                nb_arg++;
            i++;
        }
        if (nb_arg <= 5)
        {
            args = sArgs;
        } else {
            args = (char**) malloc((nb_arg + 2) * sizeof(char*));
            free_args = true;
        }
        unsigned int arg_idx = 0;
        unsigned int arg_i = 0;
        nb_arg = 1;
        args[0] = (char*) malloc(2056);
        i = 0;
        while (cmd_block[i] == ' ')
            i++;
        while (cmd_block[i] != '\n')
        {
            if (cmd_block[i] == ';')
            {
                args[arg_idx][arg_i] = 0;
                arg_idx++;
                nb_arg++;
                args[arg_idx] = (char*) malloc(2056);
                arg_i = 0;
                i++;
                continue;
            }
            args[arg_idx][arg_i] = cmd_block[i];
            arg_i++;
            i++;
        }
        args[arg_idx][arg_i] = 0;
    }
    s_debug("Executing command : %s - %d args\n", cmd, nb_arg);
    int64_t expected_size = execute_command(client, cmd, args, nb_arg);
    if (client->state != WRITE_ERROR)
    {
        if (client->state == EXPECTING_BINARY_DATA)
        {
            s_debug("binary command : %jd\n", expected_size);
            if (expected_size < 0) {
                client->shallow_binary_block = true;
                expected_size = expected_size * -1;
            }
            client->expected_block_size = expected_size;
            client->binary_block = NULL;
            client->binary_block_size = 0;
            client->binary_header_size = 0;
        }
        else {
            client->state = WAITING_FOR_COMMAND;
        }
    }
    for (unsigned int i = 0; i < nb_arg; i++)
    {
        free(args[i]);
    }
    if (free_args)
        free(args);
    free(cmd);
}


static bool preprocess_data(generic_poll_server_client* client)
{
    unsigned int read_pos = 0;
    s_debug("Pre:Read size : %d\n", client->readed_size);
    s_debug("Data : %s\n", hexString(client->readed_data, client->readed_size));
    while (read_pos == 0 || read_pos < client->readed_size)
    {
        s_debug("Client in state : %d\n", client->state);
        if (client->state == EXPECTING_BINARY_DATA)
        {
            client->state = GETTING_BINARY_DATA;
            client->binary_header_size = 0;
            memset(client->binary_header, 0, 5);
            if (client->readed_data[read_pos] != 0)
            {
                send_error(client->socket_fd, protocol_error, "Sending invalid binary data block");
                return false;
            }
        }
        if (client->state == GETTING_BINARY_DATA)
        {
            if (client->binary_header_size < 5) // We need first to get the header
            {
                while (read_pos < client->readed_size && client->binary_header_size < 5)
                {
                    client->binary_header[client->binary_header_size] = client->readed_data[read_pos];
                    read_pos++;
                    client->binary_header_size++;
                }
                if (client->binary_header_size == 5)
                {
                    s_debug("Binary block header data : %s\n", hexString(client->binary_header, 5));
                    client->binary_block_defined_size = ntohl(*((uint32_t*)(client->binary_header + 1)));
                    client->binary_block = (char*) malloc(client->binary_block_defined_size);
                    client->binary_block_size = 0;
                    s_debug("Binary defined block size : %d\n", client->binary_block_defined_size);
                    if (!(client->binary_block_defined_size == client->expected_block_size || client->expected_block_size == 0))
                    {
                        send_error(client->socket_fd, protocol_error, "Binary block size does not match expected size");
                        return false;
                    }
                }
            }
            // If we have the header, we can copy stuff into the right buffer
            if (client->binary_header_size == 5 && read_pos < client->readed_size)
            {
                unsigned int copy_size = MIN(client->readed_size - read_pos, client->binary_block_defined_size);
                memcpy(client->binary_block + client->binary_block_size, client->readed_data + read_pos, copy_size);
                client->binary_block_size += copy_size;
                read_pos += copy_size;
                s_debug("Binary block size : %d\n", client->binary_block_size);
                if (client->binary_block_size == client->binary_block_defined_size)
                {
                    if (!client->shallow_binary_block)
                    {
                        process_binary_block(client);
                    } else {
                        s_debug("Shallowing the binary block\n");
                        free(client->binary_block);
                        client->binary_block = NULL;
                        client->binary_block_size = 0;
                        client->binary_block_defined_size = 0;
                        client->state = WAITING_FOR_COMMAND;
                    }
                    client->shallow_binary_block = false;
                    continue;
                }
            }
        }
        if (client->state == WAITING_FOR_COMMAND)
        {
            if (client->readed_data[read_pos] == '!' || isalnum(client->readed_data[read_pos]) != 0)
            {
                client->state = GETTING_COMMAND;
            } else {
                send_error(client->socket_fd, protocol_error, "Expecting a command or a signal registration");
                return false;
            }
        }
        if (client->state == GETTING_COMMAND)
        {
            for (unsigned int i = read_pos; i < client->readed_size; i++)
            {
                client->command_data[client->command_data_pos] = client->readed_data[i];
                client->command_data_pos++;
                read_pos++;
                if (client->readed_data[i] == '\n')
                {
                    client->command_data_size = client->command_data_pos;
                    if (client->command_data[0] == 'b')
                    {
                        client->state = EXPECTING_BINARY_DATA;
                    }
                    process_command(client);
                    client->command_data_pos = 0;
                    client->command_data_size = 0;
                    s_debug("read pos: %d\n", read_pos);
                    break;
                }
            }
        }
    }
    s_debug("End preprocess data\n");
    return true;
}

// CORE;_MEMOR;Y_WRITE blalblalb\ndata;
// COREMWRITE blablalba\ndata;


static bool read_client_data(SOCKET fd)
{
    char    buff[2048];

    int read_size;
    generic_poll_server_client* client = get_client(fd);
    read_size = read(fd, buff, 2048);
    s_debug("Readed %d data on %d\n", read_size, fd);
    if (read_size <= 0)
        return false;
    client->readed_size = (unsigned int) read_size;
    memcpy(client->readed_data, buff, read_size);
    if (preprocess_data(client) == false)
    {
        close(fd);
        return false;
    }
    return true;
}
/*
long long milliseconds_now() {
#if defined WIN32 || defined _WIN32
    static LARGE_INTEGER s_frequency;
    static BOOL s_use_qpc = TRUE;
    QueryPerformanceFrequency(&s_frequency);
    if (s_use_qpc) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (1000LL * now.QuadPart) / s_frequency.QuadPart;
    } else {
        return GetTickCount();
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)(ts.tv_nsec / 1000000) + ((long long)ts.tv_sec * 1000ll);
#endif
}
*/

static bool    generic_poll_server_stop = false;

static bool generic_poll_server_start(int poll_timeout)
{
    SOCKET server_socket;
    struct sockaddr_in6 name;
	unsigned short port_range_start = EMULATOR_NETWORK_ACCESS_STARTING_PORT;

    for (unsigned int i = 0; i < 5; i++)
        clients[i].socket_fd = 0;
    server_socket = socket(AF_INET6, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        print_socket_error("Error creating the server socket\n");
        return false;
    }
#ifdef __WIN32__
    DWORD no = 0; // Probably not very unix like
    int set_result = setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &no, sizeof(no));
    if (set_result == SOCKET_ERROR)
#else
    int no = 0;
    int set_result = setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&no, sizeof(no));
    if (set_result == -1)
#endif
    {
        print_socket_error("Error setting socket to not ipv6 only\n");
        return false;
    }
#if __STDC__ > 201112L
	size_t required_size;
	getenv_s(&required_size, NULL, 0, "NWA_PORT_RANGE");
	if (required_size != 0)
	{
		char* buffer = (char*) malloc(required_size);
		getenv_s(&required_size, buffer, required_size, "NWA_PORT_RANGE");
#else
        char* buffer = getenv("NWA_PORT_RANGE");
        if (buffer != NULL)
        {
#endif
		unsigned short env_port = atoi(buffer);
		if (env_port != 0)
			port_range_start = env_port;
		else
		{
			fprintf(stderr, "Trying to read the port range from NWA_PORT_RANGE variable but not a valid port : %s\n", buffer);
		}
#if __STDC__ > 201112L
        free(buffer);
#endif
	}

    memset(&name, 0, sizeof(name));
    name.sin6_family = AF_INET6;
    name.sin6_port = htons(port_range_start);
    name.sin6_addr = in6addr_any;
    unsigned int cpt = 0;
    int bind_return = bind(server_socket, (struct sockaddr*) &name, sizeof(name));
    while (bind_return < 0 && cpt != 4)
    {
        cpt++;
        printf("Can't bind the socket on port %s, %d, trying next port\n", strerror(errno), port_range_start + cpt);
        name.sin6_port = htons(port_range_start + cpt);
        bind_return = bind(server_socket, (struct sockaddr*) &name, sizeof(name));
    }
    if (bind_return < 0)
    {
        print_socket_error("Can't bind socket");
        return false;
    }
    if (listen(server_socket, 1) < 0)
    {
        print_socket_error("Can't listen");
        return false;
    }
    if (callbacks.server_started != NULL)
        callbacks.server_started(port_range_start + cpt);
    //long long now = milliseconds_now();
    //s_debug("Time : %lld\n", milliseconds_now());
    struct pollfd   poll_fds[6];
    unsigned int    poll_fds_count = 1;
    poll_fds[0].fd = server_socket;
    poll_fds[0].events = POLLIN;
    while (true)
    {
        //s_debug("Waiting for poll\n");
        if (generic_poll_server_stop)
        {
            s_debug("Stopping the server\n");
            for (unsigned int i = 0; i < 5; i++)
            {
                if (clients[i].socket_fd != 0)
                {
                    close(clients[i].socket_fd);
                    clients[i].socket_fd = 0;
                }
            }
            close(server_socket);
            generic_poll_server_stop = false;
            return true;
        }
        int ret = poll(poll_fds, poll_fds_count, poll_timeout);
        if (callbacks.after_poll != NULL)
            callbacks.after_poll();
        //s_debug("%lld - Poll returned : %d\n", milliseconds_now() - now, ret);
        if (ret < 0)
        {
            fprintf(stderr, "Error polling: %s\n", strerror(errno));
            if (errno == EINTR) break; // FIXME: or continue?
            return false;
        }
        // New clients ?
        if (poll_fds[0].revents & POLLIN)
        {
            printf("New client connection\n");
            SOCKET new_socket;
            socklen_t size = sizeof(struct sockaddr_in6);
            struct sockaddr_in6 new_client;
            new_socket = accept(server_socket, (struct sockaddr *) &new_client, &size);
            if (new_socket < 0)
            {
                print_socket_error("Error while accepting new client");
                return false;
            }
            if (add_to_clients(new_socket))
            {
                unsigned long piko = 1;
                ioctlsocket(new_socket, FIONBIO, &piko);
                int addrlen = sizeof(new_client);
                getpeername(new_socket, (struct sockaddr *)&new_client, &addrlen);
                char str[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, &new_client.sin6_addr, str, sizeof(str))) {
                    s_debug("Client address is %s : %d\n", str, ntohs(new_client.sin6_port));
                }
                poll_fds[poll_fds_count].fd = new_socket;
                poll_fds[poll_fds_count].events = POLLIN;
                poll_fds[poll_fds_count].revents = 0;
                poll_fds_count++;
                if (callbacks.add_client != NULL)
                    callbacks.add_client(new_socket);
            } else {
                fprintf(stderr, "execing max client number : %d\n", 5);
                close(new_socket);
            }
        }

        for (unsigned int i = 1; i < poll_fds_count; i++)
        {
            // This is when the socket is not closed nicely
            // Check if POLLIN and POLLERR can happen at the same time
            if (poll_fds[i].revents & POLLERR || poll_fds[i].revents & POLLHUP)
            {
                s_debug("Disconnecting client err(%X)/hup(%X) : %X\n", POLLERR, POLLHUP, poll_fds[i].revents);
                remove_client(poll_fds[i].fd);
                if (i != poll_fds_count - 1)
                    poll_fds[i] = poll_fds[poll_fds_count - 1];
                poll_fds_count--;
                i--;
                continue;
            }
            if (poll_fds[i].revents & POLLIN)
            {
                s_debug("New data on client : %d\n", i);
                if (read_client_data(poll_fds[i].fd) == false)
                {
                    s_debug("Disconnecting client\n");
                    remove_client(poll_fds[i].fd);
                    if (i != poll_fds_count - 1)
                        poll_fds[i] = poll_fds[poll_fds_count - 1];
                    poll_fds_count--;
                    i--;
                }
            }
        }
    }
    return true;
}
