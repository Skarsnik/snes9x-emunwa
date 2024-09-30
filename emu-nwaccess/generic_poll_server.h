#ifndef H_GENERIC_POLL_SERVER_H
#define H_GENERIC_POLL_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "emulator_network_access_defines.h"

#ifdef _WIN32
#include <winsock2.h>
#else
typedef int SOCKET;
#endif

/*
The function doing the command must returns this :
    0 for regular command
    > 0 for binary command, that corresponds to the expected size
    < 0 for binary command if there is an error, this is the expected size
                           this is needed so the code can properly swallow
                           the next binary block
*/

typedef struct
{
    emulator_network_access_command command;
    int64_t(*function)(SOCKET, char**, int);
} generic_emu_nwa_command_entry;

typedef struct
{
    const char* string;
    int64_t(*function)(SOCKET, char**, int);
} custom_emu_nwa_command_entry;


typedef struct generic_poll_server_memory_argument_struct
{
    unsigned int offset;
    unsigned int size;
    struct generic_poll_server_memory_argument_struct* next;
} generic_poll_server_memory_argument;

typedef generic_emu_nwa_command_entry generic_emu_nwa_commands_map_t[];

typedef custom_emu_nwa_command_entry custom_emu_nwa_commands_map_t[];



typedef enum
{
    NEW_CLIENT,
    REMOVED_CLIENT,
    SERVER_STARTED,
    AFTER_POLL
}  generic_poll_server_callback;

typedef enum
{
    WAITING_FOR_COMMAND,
    GETTING_COMMAND,
    EXPECTING_BINARY_DATA,
    GETTING_BINARY_DATA,
    WRITE_ERROR
} generic_poll_server_client_state;

typedef struct {
    bool(*add_client)(SOCKET socket);
    bool(*remove_client)(SOCKET socket);
    bool(*server_started)(int port);
    bool(*after_poll)();
} generic_poll_server_callbacks;

typedef struct {
    SOCKET  socket_fd;
    char    readed_data[2048];
    char    command_data[2048];
    int     readed_size;
    int     command_data_size;
    int     command_data_pos;
    bool    shallow_binary_block;

    generic_poll_server_client_state state;

    unsigned int        current_command;

    char                binary_header[5];
    char                binary_header_size;
    char*               binary_block;
    unsigned int        binary_block_defined_size; // The size defined by the header
    unsigned int        binary_block_size; // The current size filled into the buffer
    
    unsigned int        expected_block_size;
} generic_poll_server_client;



void            generic_poll_server_add_callback(generic_poll_server_callback cb, void* fntptr);

/*
    use it like that :
    send_hash_reply(socket, "key", "value", "key2", "value2")
*/

bool            generic_poll_server_send_hash_reply(SOCKET socket, int key_count, ...);
bool            generic_poll_server_send_full_hash_reply(SOCKET socket, int key_count, ...);
bool            generic_poll_server_end_hash_reply(SOCKET socket);
bool            generic_poll_server_start_hash_reply(SOCKET socket);
bool            generic_poll_server_send_binary_block(SOCKET socket, uint32_t size, const char* data);
size_t          generic_poll_server_get_offset(const char *offset_str);
generic_poll_server_memory_argument* generic_poll_server_parse_memory_argument(const char** ag, unsigned int ac);
void            generic_poll_server_free_memory_argument(generic_poll_server_memory_argument* tofree);
bool            generic_poll_server_send_binary_header(SOCKET socket, uint32_t size);

// Use this to notify when you encounter an error on write, that allows the server to
// properly discard the client
void            generic_poll_server_set_client_state_error(SOCKET socket);

#endif
