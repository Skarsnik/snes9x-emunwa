#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <sys/types.h>

#define _WINSOCKAPI_

#include "snes9x.h"
#include "memmap.h"
#include "snes9x-nwaccess.h"
 

#pragma comment(lib, "Ws2_32.lib")

#ifdef __WIN32__

#elif defined (linux) 

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> /* close */
#include <netdb.h> /* gethostbyname */
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)
typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
typedef struct in_addr IN_ADDR;

#else

#error not defined for this platform

#endif

#ifdef USE_THREADS
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#endif

#include "emu-nwaccess/emulator-networkaccess/c_includes/emulator_network_access_defines.h"
#include "emu-nwaccess/emulator-networkaccess/generic poll server/generic_poll_server.h"


#define StartHashReply(S) generic_poll_server_start_hash_reply(S)
#define SendFullHashReply(S, K, ...) generic_poll_server_send_full_hash_reply(S, K, __VA_ARGS__)
#define EndHashReply(S) generic_poll_server_end_hash_reply(S)
#define SendHashReply(S, K, ...) generic_poll_server_send_hash_reply(S, K, __VA_ARGS__)



static char*	hexString(const char* str, const unsigned int size)
{
    char* toret = (char*) malloc(size * 3 + 1);

    unsigned int i;
    for (i = 0; i < size; i++)
    {
        sprintf(toret + i * 3, "%02X ", (unsigned char)str[i]);
    }
    toret[size * 3] = 0;
    return toret;
}

const generic_emu_nwa_commands_map_t generic_emu_mwa_map = {
    {EMU_INFO, EmuNWAccessEmuInfo},
    {EMU_STATUS, EmuNWAccessEmuStatus},
    {EMU_PAUSE, EmuNWAccessEmuPause},
    {EMU_STOP, EmuNWAccessEmuStop},
    {EMU_RESET, EmuNWAccessEmuReset},
    {EMU_RESUME, EmuNWAccessEmuResume},
    {EMU_RELOAD, EmuNWAccessEmuReload},
    {LOAD_GAME, EmuNWAccessLoadGame},
    {GAME_INFO, EmuNWAccessGameInfo},
    {CORES_LIST, EmuNWAccessCoreList},
    {CORE_INFO, EmuNWAccessCoreInfo},
    {CORE_CURRENT_INFO, EmuNWAccessCoreCurrentInfo},
    {LOAD_CORE, EmuNWAccessCoreLoad},
    {CORE_MEMORIES, EmuNWAccessCoreMemories},
    {CORE_READ, EmuNWAccessCoreRead},
    {CORE_WRITE, EmuNWAccessCoreWrite},
    {DEBUG_BREAK, EmuNWAccessNop},
    {DEBUG_CONTINUE, EmuNWAccessNop},
    {LOAD_STATE, EmuNWAccessLoadState},
    {SAVE_STATE, EmuNWAccessSaveState}
};

const unsigned int generic_emu_mwa_map_size = 20;
bool(*generic_poll_server_write_function)(SOCKET, char*, uint32_t) = &EmuNWAccessWriteToMemory;

#define SKARSNIK_DEBUG 1
#include "emu-nwaccess/emulator-networkaccess/generic poll server/generic_poll_server.c"

struct NetworkAccessInfos NetworkAccessData;

#include <list>

static void create_thread()
{

}

bool S9xNWAccessInit()
{
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(1, 0), &data) != 0)
    {
        S9XSetNWAError("Call to init Windows sockets failed. Do you have WinSock2 installed?");
        return (FALSE);
    }
    printf("WSAStartup done\n");
    NetworkAccessData.initalized = true;
    NetworkAccessData.messageMutex = CreateMutex(NULL, TRUE, NULL);
    ReleaseMutex(NetworkAccessData.messageMutex);
#endif
    generic_poll_server_add_callback(SERVER_STARTED, &EmuNWAccessServerStarted);
    generic_poll_server_add_callback(NEW_CLIENT, &EmuNWAccessNewClient);
    generic_poll_server_add_callback(REMOVED_CLIENT, &EmuNWAccessRemoveClient);
    NetworkAccessData.message[0] = 0;
	create_thread();
    return true;
}


bool	S9xNWAccessStart()
{
    if (NetworkAccessData.initalized == false)
        S9xNWAccessInit();
    uintptr_t thread;
    thread = _beginthread(S9xNWAServerLoop, 0, NULL);
    NetworkAccessData.thread = thread;
    return (thread != (uintptr_t)(~0));
}


void	S9xNWAServerLoop(void *)
{
    fprintf(stdout, "Starting generic server\n");
    generic_poll_server_start();
}

bool EmuNWAccessServerStarted(int port)
{
    char *animals[] = {"Pony", "Goose", "Squirrel", "Chicken", "Horse", "Duck", "Cat", "Dog", "Sliver"};
    char *emotions[] = {"Angry", "Smiling", "Happy", "Obsessed", "Hungry", "Special", "Drunk"};

    if (Settings.NWAccessSeed == 0)
    {
        Settings.NWAccessSeed = (unsigned int)rand();
    }
    srand(Settings.NWAccessSeed + port);
    unsigned int a_i = rand() % 9;
    unsigned int e_i = rand() % 7;
    unsigned int size = strlen(animals[a_i]) + strlen(emotions[e_i]) + 2;
    NetworkAccessData.id = (char*) malloc(size);
    snprintf(NetworkAccessData.id, size, "%s %s", emotions[e_i], animals[a_i]);
    return false;
}

bool	S9xNWAccessStop()
{
    // TODO
    return true;
}

void S9XSetNWAError(const char * msg)
{
    fprintf(stderr, "%s\n", msg);
}

bool    S9xNWAGuiLoop()
{
    DWORD dwWaitResult = WaitForSingleObject(
        NetworkAccessData.messageMutex,
        0L);
    if (dwWaitResult == WAIT_OBJECT_0)
    {
        if (NetworkAccessData.message[0] != 0)
        {
            S9xMessage(S9X_INFO, 42, NetworkAccessData.message);
            s_debug("Trying to show message to OSD : %s\n", NetworkAccessData.message);
            NetworkAccessData.message[0] = 0;
        }
        ReleaseMutex(NetworkAccessData.messageMutex);
    }
    return true;
}

void EmuNWAccessNewClient(SOCKET socket)
{
    for (unsigned int i = 0; i < 5; i++)
    {
        if (NetworkAccessData.clients[i].used == false)
        {
            auto& client = NetworkAccessData.clients[i];
            client.used = true;
            client.socket = socket;
            client.MemorySizeToWrite = 0;
            client.MemorySizeWritten = 0;
            client.MemoryToWrite = nullptr;
            client.connected = true;
            EmuNWAccessSetMessage("New client connected");
            return ;
        }
    }
}

void    EmuNWAccessSetMessage(char* msg)
{
    DWORD dwWaitResult = WaitForSingleObject(
        NetworkAccessData.messageMutex,
        1000);
    if (dwWaitResult == WAIT_OBJECT_0)
    {
        strncpy(NetworkAccessData.message, msg, strlen(msg) + 1);
        ReleaseMutex(NetworkAccessData.messageMutex);
    }
}

void EmuNWAccessRemoveClient(SOCKET socket)
{
    for (unsigned int i = 0; i < 5; i++)
    {
        if (NetworkAccessData.clients[i].socket == socket)
            NetworkAccessData.clients[i].used = true;
        return ;
    }
}

NetworkAccessClient* EmuNWAccessGetClient(SOCKET socket)
{
    for (unsigned int i = 0; i < 5; i++)
    {
        if (NetworkAccessData.clients[i].socket == socket)
            return &NetworkAccessData.clients[i];
    }
    return nullptr;
}


bool EmuNWAccessEmuInfo(SOCKET socket, char ** args, int ac)
{
    SendFullHashReply(socket, 3, "name", "Snes9x",
                              "version", "1.60-nwa",
                              "id", NetworkAccessData.id);
    return true;
}

bool EmuNWAccessEmuStatus(SOCKET socket, char ** args, int ac)
{
    StartHashReply(socket);
    if (Settings.Paused)
    {
        SendHashReply(socket, 1, "state", "paused");
    } else {
        if (Settings.StopEmulation)
            SendHashReply(socket, 1, "state", "stopped");
        else
            SendHashReply(socket, 1, "state", "running");
    }
    if (Memory.ROMName[0] != 0)
    {
        SendHashReply(socket, 1, "game", Memory.ROMName);
    } else {
        SendHashReply(socket, 1, "game", "");
    }
    EndHashReply(socket);
    return true;
}

bool EmuNWAccessEmuPause(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessEmuStop(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessEmuReset(SOCKET socket, char ** args, int ac)
{
    //S9xSoftReset();
    return false;
}

bool EmuNWAccessEmuResume(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessEmuReload(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessLoadGame(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessGameInfo(SOCKET socket, char ** args, int ac)
{
    SendFullHashReply(socket, 5, "name", Memory.ROMName,
                             "file", Memory.ROMFilename,
                             "region", Memory.Country(),
                             "type", Memory.MapType(),
                             "video-ouput", (Memory.ROMRegion > 12 || Memory.ROMRegion < 2) ? "NTSC 60Hz" : "PAL 50Hz");
    return true;
}

bool EmuNWAccessCoreList(SOCKET socket, char ** args, int ac)
{
   SendFullHashReply(socket, 2, "name", "snes9x", 
                                "platform", "SNES");
   return true;
}

bool EmuNWAccessCoreInfo(SOCKET socket, char ** args, int ac)
{
    SendFullHashReply(socket, 3, "name", "snes9x",
                                 "version", VERSION,
                                 "platform", "SNES");
    return true;
}

bool EmuNWAccessCoreCurrentInfo(SOCKET socket, char ** args, int ac)
{
    return EmuNWAccessCoreInfo(socket, args, ac);
}

bool EmuNWAccessCoreLoad(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessCoreMemories(SOCKET socket, char ** args, int ac)
{
    SendFullHashReply(socket, 4 * 2, "name", "WRAM", "access", "rw",
                                     "name", "SRAM", "access", "rw",
                                     "name", "CARTROM", "access", "rw",
                                     "name", "VRAM", "access", "rw");
    return true;
}

bool EmuNWAccessCoreRead(SOCKET socket, char ** args, int ac)
{
    uint8* to_read = NULL;
    if (strcmp(args[0],"WRAM") == 0)
        to_read = Memory.RAM;
    if (strcmp(args[0], "SRAM") == 0)
        to_read = Memory.SRAM;
    if (strcmp(args[0], "CARTROM") == 0)
        to_read = Memory.ROM;
    if (strcmp(args[0], "VRAM") == 0)
        to_read = Memory.VRAM;
    if (to_read == NULL)
    {
        send_error(socket, "No matching Memory Domain name");
        return false;
    }
    size_t offset = generic_poll_server_get_offset(args[1]);
    uint32_t size = atoi(args[2]);
    write(socket, "\0", 1);
    uint32_t network_size = htonl(size);
    write(socket, (const char*)&network_size, 4);
    write(socket, (const char*)to_read + offset, size);
    return true;
}

bool EmuNWAccessWriteToMemory(SOCKET socket, char* data, uint32_t size)
{
    static bool size_header_get = false;

    auto client = EmuNWAccessGetClient(socket);
    s_debug("write_to_memory : %d, %s\n", size, hexString(data, size));
    if (size_header_get == false)
    {
        uint32_t header_size = ntohl(*((uint32_t*)data));
        if (client->MemorySizeToWrite != 0 && header_size != client->MemorySizeToWrite)
        {
            // this is an error
        }
        client->MemorySizeToWrite = header_size;
        s_debug("wtm: ntohl: %d\n", client->MemorySizeToWrite);
        data += 4;
        size -= 4;
        size_header_get = true;
    }
    if (size == 0)
        return false;
    memcpy(client->MemoryToWrite + client->MemoryOffsetToWrite, data, size);
    client->MemorySizeWritten += size;
    s_debug("Writing to memory : %d - %d/%d\n", size, client->MemorySizeWritten, client->MemorySizeWritten);
    if (client->MemorySizeWritten == client->MemorySizeWritten)
    {
        s_debug("Done Writing to memory\n");
        client->MemorySizeToWrite = 0;
        client->MemorySizeWritten = 0;
        client->MemoryOffsetToWrite = 0;
        client->MemoryToWrite = NULL;
        size_header_get = false;
        write(socket, "\n\n", 2);
        return true;
    }
    return false;
}

bool EmuNWAccessCoreWrite(SOCKET socket, char ** args, int ac)
{
    s_debug("Core Write  : %s - arg count : %d\n", args[0], ac);
    uint8* to_read = NULL;
    if (strcmp(args[0], "WRAM") == 0)
        to_read = Memory.RAM;
    if (strcmp(args[0], "SRAM") == 0)
        to_read = Memory.SRAM;
    if (strcmp(args[0], "CARTROM") == 0)
        to_read = Memory.ROM;
    if (strcmp(args[0], "VRAM") == 0)
        to_read = Memory.VRAM;
    if (to_read == NULL)
    {
        send_error(socket, "No matching Memory Domain name");
        return false;
    }
    size_t offset = generic_poll_server_get_offset(args[1]);
    auto client = EmuNWAccessGetClient(socket);
    client->MemorySizeToWrite = 0;
    if (ac == 3)
    {
        client->MemorySizeToWrite = atoi(args[2]);
    }
    client->MemorySizeWritten = 0;
    client->MemoryToWrite = to_read;
    client->MemoryOffsetToWrite = offset;
    return true;
}

bool EmuNWAccessNop(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessLoadState(SOCKET socket, char ** args, int ac)
{
    return false;
}

bool EmuNWAccessSaveState(SOCKET socket, char ** args, int ac)
{
    return false;
}
