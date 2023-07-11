#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <sys/types.h>

#define _WINSOCKAPI_

// Don't include snes9x before snes9x-nwaccess, gtk_conpat.h fail if so'
#include "snes9x-nwaccess.h"

#include "snes9x.h"
#include "memmap.h"

#include "snapshot.h"
#include "display.h"


#pragma comment(lib, "Ws2_32.lib")

#ifdef __WIN32__
#include "win32_sound.h"
#include "wsnes9x.h"

#elif (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__)))

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
    {EMULATOR_INFO, EmuNWAccessEmuInfo},
    {EMULATION_STATUS, EmuNWAccessEmuStatus},
    {EMULATION_PAUSE, EmuNWAccessEmuPause},
    {EMULATION_STOP, EmuNWAccessEmuStop},
    {EMULATION_RESET, EmuNWAccessEmuReset},
    {EMULATION_RESUME, EmuNWAccessEmuResume},
    {EMULATION_RELOAD, EmuNWAccessEmuReload},
    {MY_NAME_IS, EmuNWAccessSetClientName},
    {LOAD_GAME, EmuNWAccessLoadGame},
    {GAME_INFO, EmuNWAccessGameInfo},
    {CORES_LIST, EmuNWAccessCoreList},
    {CORE_INFO, EmuNWAccessCoreInfo},
    {CORE_CURRENT_INFO, EmuNWAccessCoreCurrentInfo},
    {LOAD_CORE, EmuNWAccessCoreLoad},
    {CORE_MEMORIES, EmuNWAccessCoreMemories},
    {CORE_READ, EmuNWAccessCoreRead},
    {bCORE_WRITE, EmuNWAccessCoreWrite},
    {DEBUG_BREAK, EmuNWAccessNop},
    {DEBUG_CONTINUE, EmuNWAccessNop},
    {LOAD_STATE, EmuNWAccessLoadState},
    {SAVE_STATE, EmuNWAccessSaveState}
};

const unsigned int generic_emu_mwa_map_size = 21;
const custom_emu_nwa_commands_map_t custom_emu_nwa_map = {0};
const unsigned int custom_emu_nwa_map_size = 0;
bool(*generic_poll_server_write_function)(SOCKET, char*, uint32_t) = &EmuNWAccessWriteToMemory;

#ifdef _DEBUG 
#define SKARSNIK_DEBUG 1
#endif
#include "emu-nwaccess/emulator-networkaccess/generic poll server/generic_poll_server.c"

struct NetworkAccessInfos NetworkAccessData;

#include <list>

/*
 * This is a list of function for to wrap windows defined snes9x functions
 *
 */

#ifdef SNES9X_GTK
void    S9xSetPause(unsigned int)
{
    NetworkAccessData.gtkWindow->pause_from_user();
}

void S9xClearPause(unsigned int)
{
    NetworkAccessData.gtkWindow->unpause_from_user();
}

void    S9xNWASetGtkWindow(Snes9xWindow* window)
{
    NetworkAccessData.gtkWindow = window;
}
#endif

// This should run once
bool S9xNWAccessInit()
{
    if (NetworkAccessData.initalized)
        return false;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(1, 0), &data) != 0)
    {
        S9XSetNWAError("Call to init Windows sockets failed. Do you have WinSock2 installed?");
        return (FALSE);
    }
    printf("WSAStartup done\n");
    NetworkAccessData.messageMutex = false;
    //ReleaseMutex(NetworkAccessData.messageMutex);
#endif
    generic_poll_server_add_callback(SERVER_STARTED, (void*)(&EmuNWAccessServerStarted));
    generic_poll_server_add_callback(NEW_CLIENT, (void*)(&EmuNWAccessNewClient));
    generic_poll_server_add_callback(REMOVED_CLIENT, (void*)(&EmuNWAccessRemoveClient));
    generic_poll_server_add_callback(AFTER_POLL, (void*)(&EmuNWAccessAfterPoll));
    //create_thread();
    NetworkAccessData.initalized = true;

    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_LEAK_CHECK_DF);

    return true;
}

void S9xNWAccessInitData()
{
    NetworkAccessData.stopRequest = false;
    NetworkAccessData.controlCommandTriggerMutex = false;
    NetworkAccessData.controlCommandDone = false;
    NetworkAccessData.stateTriggerMutex = false;
    NetworkAccessData.stateDoneMutex = false;
    NetworkAccessData.saveStatePath = nullptr;
    NetworkAccessData.romToLoad = nullptr;
    NetworkAccessData.messageMutex = false;
    NetworkAccessData.message[0] = 0;
}

#ifdef SNES9X_GTK
static gpointer GThreadServerStart(gpointer)
{
    S9xNWAServerLoop(NULL);
    return NULL;
}
#endif

bool	S9xNWAccessStart()
{
    if (NetworkAccessData.initalized == false)
        S9xNWAccessInit();
    S9xNWAccessInitData();
#ifdef __WIN32__
    uintptr_t thread;
    thread = _beginthread(S9xNWAServerLoop, 0, NULL);
#elif defined(SNES9X_GTK)
    GThread* thread = g_thread_new(NULL, GThreadServerStart, NULL);
#endif
    NetworkAccessData.thread = thread;
    return (thread != (THREAD_TYPE)(~0));
}

bool	S9xNWAccessStop()
{
    NetworkAccessData.stopRequest = true;
    #ifdef __WIN32__
    WaitForSingleObject((HANDLE)NetworkAccessData.thread, 100);
    // #elif defined(SNES9X_GTK)
    // pthread_timedjoin_np((HANDLE)NetworkAccessData.thread, NULL, 100);
    #endif
    return true;
}


void	S9xNWAServerLoop(void *)
{
    fprintf(stdout, "Starting generic server\n");
    generic_poll_server_start(50);
}

bool EmuNWAccessServerStarted(int port)
{
    fprintf(stdout, "NetworkAccess started on port %d\n", port);
    const char *animals[] = {"Pony", "Goose", "Squirrel", "Chicken", "Horse", "Duck", "Cat", "Dog", "Sliver"};
    const char *emotions[] = {"Angry", "Smiling", "Happy", "Obsessed", "Hungry", "Special", "Drunk"};

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


/* This allow to do thing every 200 ms or when an event on socket happen
This is where we reply to all the control comand and savestate
Commands are done in the gui loop function.
*/
bool EmuNWAccessAfterPoll()
{
    if (NetworkAccessData.stateDoneMutex)
    {
        s_debug("Replying to state command\n");
        if (NetworkAccessData.stateError)
            send_error(NetworkAccessData.stateTriggerClient, command_error, "Unable to do the save/load state operation");
        else
            write(NetworkAccessData.stateTriggerClient, "\n\n", 2);
        NetworkAccessData.stateDoneMutex = false;
    }
    if (NetworkAccessData.controlCommandDone)
    {
        if (NetworkAccessData.controlError)
            send_error(NetworkAccessData.controlClient, command_error, "Unable to perform the requested control command");
        else
            write(NetworkAccessData.controlClient, "\n\n", 2);
        if (NetworkAccessData.command == NetworkAccessControlCommand::CMD_LOAD)
            ;// free(NetworkAccessData.romToLoad); //FIXME, this trigger a heap corruption?
        NetworkAccessData.controlCommandDone = false;
    }
    if (NetworkAccessData.stopRequest)
    {
        generic_poll_server_stop = true;
        NetworkAccessData.stopRequest = false;
    }
    return true;
}

void S9XSetNWAError(const char * msg)
{
    fprintf(stderr, "%s\n", msg);
}

bool    S9xNWAGuiLoop()
{
    if (NetworkAccessData.messageMutex)
    {
        if (NetworkAccessData.message[0] != 0)
        {
            S9xSetInfoString(NetworkAccessData.message);
            S9xMessage(S9X_INFO, 42, NetworkAccessData.message);
            s_debug("Trying to show message to OSD : %s\n", NetworkAccessData.message);
            NetworkAccessData.message[0] = 0;
        }
        NetworkAccessData.messageMutex = false;
    }

    if (NetworkAccessData.stateTriggerMutex)
    {
        NetworkAccessData.stateDoneMutex = false;
        NetworkAccessData.stateError = false;
        if (Settings.StopEmulation)
        {
            NetworkAccessData.stateError = true;
        } else {
            S9xSetPause(PAUSE_FREEZE_FILE);
            // Save State
            if (NetworkAccessData.saveState)
            {
                S9xFreezeGame(NetworkAccessData.saveStatePath);
            }
            else { // Load State
                S9xUnfreezeGame(NetworkAccessData.saveStatePath);
            }
            S9xClearPause(PAUSE_FREEZE_FILE);
        }
        NetworkAccessData.stateDoneMutex = true;
        NetworkAccessData.stateTriggerMutex = false;
    }
    if (NetworkAccessData.controlCommandTriggerMutex)
    {
        NetworkAccessData.controlCommandDone = false;
        NetworkAccessData.controlCommandError = false;
        if ((NetworkAccessData.command == NetworkAccessControlCommand::CMD_RESUME ||
            NetworkAccessData.command == NetworkAccessControlCommand::CMD_PAUSE) &&
            Settings.StopEmulation)
        {
            NetworkAccessData.controlCommandError = true;
        }
        else {
            switch (NetworkAccessData.command)
            {
            case NetworkAccessControlCommand::CMD_PAUSE:
            {
                S9xSetPause(1);
                break;
                //Settings.Paused = Settings.Paused ^ true;
                //Settings.FrameAdvance = false;
            }
            case NetworkAccessControlCommand::CMD_STOP:
            {
                Settings.StopEmulation = TRUE;
                break;
            }
            case NetworkAccessControlCommand::CMD_RESUME:
            {
                S9xClearPause(1);
                break;
            }
            case NetworkAccessControlCommand::CMD_RESET:
            {
                S9xSoftReset();
#ifdef __WIN32__
                ReInitSound();
#endif
                break;
            }
            case NetworkAccessControlCommand::CMD_RELOAD:
            {
                S9xReset();
                break;
            }
            case NetworkAccessControlCommand::CMD_LOAD:
            {
                #ifdef __WIN32__
                TCHAR romFile[2048];
                int nb_backslash = 0;
                /* Snes9x code expect path with \\ */
                for (unsigned int i = 0; i < strlen(NetworkAccessData.romToLoad); i++)
                {
                    if (NetworkAccessData.romToLoad[i] == '\\')
                    {
                        nb_backslash++;
                    }
                }
                char* fixPath = (char*) malloc(strlen(NetworkAccessData.romToLoad) + nb_backslash + 1);
                unsigned j = 0;
                for (unsigned int i = 0; i < strlen(NetworkAccessData.romToLoad); i++)
                {
                    fixPath[j] = NetworkAccessData.romToLoad[i];
                    if (fixPath[j] == '\\')
                    {
                        j++;
                        fixPath[j] = '\\';
                    }
                    j++;
                }
                fixPath[j + 1] = 0;
                swprintf(romFile, strlen(fixPath), L"%hs", fixPath);
                wsnes9xLoadROM(romFile, NULL);
                #endif
                break;
            }
            }
        }
        NetworkAccessData.controlCommandDone = true;
        NetworkAccessData.controlCommandTriggerMutex = false;
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
            client.MemoryToWrite = nullptr;
            client.MemoryWriteInfos = NULL;
            client.connected = true;
            EmuNWAccessSetMessage("New client connected");
            return ;
        }
    }
}

void    EmuNWAccessSetMessage(char* msg)
{
    if (!NetworkAccessData.messageMutex)
    {
        strncpy(NetworkAccessData.message, msg, strlen(msg) + 1);
        NetworkAccessData.messageMutex = true;
    }
}

void EmuNWAccessRemoveClient(SOCKET socket)
{
    for (unsigned int i = 0; i < 5; i++)
    {
		if (NetworkAccessData.clients[i].socket == socket)
		{
			NetworkAccessData.clients[i].used = false;
			return;
		}
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


int64_t EmuNWAccessSetClientName(SOCKET socket, char **args, int ac)
{
    NetworkAccessClient* client = EmuNWAccessGetClient(socket);
    if (ac != 1)
    {
        send_error(socket, invalid_argument, "MY_NAME_IS take one argument");
        return true;
    }
    else {
        strcpy(client->id, args[0]);
    }
    SendFullHashReply(socket, 1, "name", client->id);
    return true;
}

int64_t EmuNWAccessEmuInfo(SOCKET socket, char ** args, int ac)
{
    char list_command[2048];
    char version[50];
    memset(list_command, 0, 2048);
    for (unsigned int i = 0; i < generic_emu_mwa_map_size; i++)
    {
        for (unsigned int j = 0; j < emulator_network_access_number_of_command; j++)
        {
            if (generic_emu_mwa_map[i].command == emulator_network_access_command_strings[j].command)
            {
                strcpy(list_command + strlen(list_command), emulator_network_access_command_strings[j].string);
                if (i != generic_emu_mwa_map_size - 1)
                    strcpy(list_command + strlen(list_command), ",");
            }
        }
    }
    snprintf(version, 50, "%s-nwa", VERSION);
    SendFullHashReply(socket, 5, "name", "Snes9x",
                                 "version", version,
                                 "nwa_version", "1.0",
                                 "id", NetworkAccessData.id,
                                 "commands", list_command);
    return 0;
}

int64_t EmuNWAccessEmuStatus(SOCKET socket, char ** args, int ac)
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
    return 0;
}

static int64_t    DoControlCommand(SOCKET socket, NetworkAccessControlCommand cmd)
{
    if (NetworkAccessData.controlCommandDone == false &&
        NetworkAccessData.controlCommandTriggerMutex == true)
    {
        send_error(socket, command_error, "Already doing a control command");
        return 0;
    }
    NetworkAccessData.controlCommandTriggerMutex = true;
    NetworkAccessData.command = cmd;
    NetworkAccessData.controlClient = socket;
    return 0;
}

int64_t EmuNWAccessEmuPause(SOCKET socket, char ** args, int ac)
{
    return DoControlCommand(socket, NetworkAccessControlCommand::CMD_PAUSE);
}

int64_t EmuNWAccessEmuStop(SOCKET socket, char ** args, int ac)
{
    return DoControlCommand(socket, NetworkAccessControlCommand::CMD_STOP);
}

int64_t EmuNWAccessEmuReset(SOCKET socket, char ** args, int ac)
{
    return DoControlCommand(socket, NetworkAccessControlCommand::CMD_RESET);
}

int64_t EmuNWAccessEmuResume(SOCKET socket, char ** args, int ac)
{
    return DoControlCommand(socket, NetworkAccessControlCommand::CMD_RESUME);
}

int64_t EmuNWAccessEmuReload(SOCKET socket, char ** args, int ac)
{
    return DoControlCommand(socket, NetworkAccessControlCommand::CMD_RELOAD);
}

int64_t EmuNWAccessLoadGame(SOCKET socket, char ** args, int ac)
{
    DoControlCommand(socket, NetworkAccessControlCommand::CMD_LOAD);
    NetworkAccessData.romToLoad = (char*) malloc(strlen(args[0]) + 1);
    strcpy(NetworkAccessData.romToLoad, args[0]);
    return 0;
}

int64_t EmuNWAccessGameInfo(SOCKET socket, char ** args, int ac)
{
    SendFullHashReply(socket, 5, "name", Memory.ROMName,
                             "file", Memory.ROMFilename,
                             "region", Memory.Country(),
                             "type", Memory.MapType(),
                             "video-ouput", (Memory.ROMRegion > 12 || Memory.ROMRegion < 2) ? "NTSC 60Hz" : "PAL 50Hz");
    return 0;
}

int64_t EmuNWAccessCoreList(SOCKET socket, char ** args, int ac)
{
    if (ac == 0 || (ac == 1 && strcmp(args[0], "SNES") == 0))
    {
        SendFullHashReply(socket, 2, "name", "snes9x",
            "platform", "SNES");
    }
    else {
        write(socket, "\n\n", 2);
    }
    return 0;
}

int64_t EmuNWAccessCoreInfo(SOCKET socket, char ** args, int ac)
{
    if (ac == 0 || (ac == 1 && strcmp(args[0], "snes9x") == 0))
    {
        SendFullHashReply(socket, 3, "name", "snes9x",
            "version", VERSION,
            "platform", "SNES");
    } else {
        send_error(socket, invalid_argument, "The specified core does not exists, only snes9x core exists");
    }
    return 0;
}

int64_t EmuNWAccessCoreCurrentInfo(SOCKET socket, char ** args, int ac)
{
    return EmuNWAccessCoreInfo(socket, args, ac);
}

int64_t EmuNWAccessCoreLoad(SOCKET socket, char ** args, int ac)
{
    return -1;
}

static unsigned int getMemorySize(const char* mem)
{
    if (strcmp(mem, "WRAM") == 0)
        return 0x20000;
    if (strcmp(mem, "SRAM") == 0)
        return Memory.SRAMSize;
    if (strcmp(mem, "CARTROM") == 0)
        return Memory.ROMSize;
    if (strcmp(mem, "VRAM") == 0)
        return 0x10000;
    return 0; // This is not reachable
}


int64_t EmuNWAccessCoreMemories(SOCKET socket, char ** args, int ac)
{
    char buf[100];
    sprintf(buf, "%d", getMemorySize("WRAM"));
    sprintf(buf + 20, "%d", getMemorySize("SRAM"));
    sprintf(buf + 40, "%d", getMemorySize("CARTROM"));
    sprintf(buf + 60, "%d", getMemorySize("VRAM"));
    SendFullHashReply(socket, 4 * 3, "name", "WRAM", "access", "rw", "size", (char*)buf,
                                     "name", "SRAM", "access", "rw", "size", (char*)(buf + 20),
                                     "name", "CARTROM", "access", "rw", "size", (char*)(buf + 40),
                                     "name", "VRAM", "access", "rw", "size", (char*)(buf + 60));
    return 0;
}

int64_t EmuNWAccessCoreRead(SOCKET socket, char ** args, int ac)
{
    std::unique_lock<std::mutex> lk(Memory.lock);

    uint8* to_read = NULL;
    s_debug("core read args : %s, %s, %s\n", args[0], args[1], args[2]);
    if (strncmp(args[0],"WRAM", 4) == 0)
        to_read = Memory.RAM;
    if (strncmp(args[0], "SRAM", 4) == 0)
    {
       s_debug("Matched SRAM : %d\n", Memory.SRAM);
       to_read = Memory.SRAM;
    }
    if (strncmp(args[0], "CARTROM", 7) == 0)
        to_read = Memory.ROM;
    if (strncmp(args[0], "VRAM", 4) == 0)
        to_read = Memory.VRAM;
    s_debug("to_read : %d\n", to_read);
    if (to_read == NULL)
    {
        send_error(socket, command_error, "No matching Memory Domain name");
        return 0;
    }
    if (ac == 1)
    {
        generic_poll_server_send_binary_block(socket, getMemorySize(args[0]), (const char*)to_read);
        return 0;
    }
    if (ac > 3 && ac % 2 != 1)
    {
        send_error(socket, invalid_argument, "You can't only ommit the size of a single write");
        return 0;
    }
    generic_poll_server_memory_argument*    pargs = generic_poll_server_parse_memory_argument(args + 1, ac - 1);
    generic_poll_server_memory_argument* cur = pargs;
    uint32_t total_size = 0;
    while (cur != NULL)
    {
        total_size += cur->size;
        if (cur->size == 0)
        {
            if (ac == 2)
            {
                cur->size = getMemorySize(args[0]) - cur->offset;
            } else {
                send_error(socket, invalid_argument, "0 size is invalid");
                generic_poll_server_free_memory_argument(pargs);
                return 0;
            }
            total_size += getMemorySize(args[0]) - cur->offset;
        }
        cur = cur->next;
    }
    s_debug("Preparing to read core %d bytes from %s\n", total_size, args[0]);
    char header[5];
    header[0] = 0;
    uint32_t network_size = htonl(total_size);
    memcpy(header + 1, (void*)&network_size, 4);
    //s_debug("Header : %s", hexString(header, 5));
    write(socket, header, 5);
    cur = pargs;
    while (cur != NULL)
    {
        unsigned int size = cur->size;
        if (size == 0)
            size = getMemorySize(args[0]) - cur->offset;
        write(socket, (const char*)to_read + cur->offset, size);
        cur = cur->next;
    }
    generic_poll_server_free_memory_argument(pargs);
    return 0;
}

bool EmuNWAccessWriteToMemory(SOCKET socket, char* data, uint32_t size)
{
    std::unique_lock<std::mutex> lk(Memory.lock);
    auto client = EmuNWAccessGetClient(socket);
    if (size == 0)
        return false;
    generic_poll_server_memory_argument* infos = client->MemoryWriteInfos;
    uint32_t offsetInData = 0;
    while (infos != NULL)
    {
        memcpy(client->MemoryToWrite + infos->offset, data + offsetInData, infos->size);
        offsetInData += infos->size;
        infos = infos->next;
    }
    generic_poll_server_free_memory_argument(client->MemoryWriteInfos);
    client->MemoryWriteInfos = NULL;
    s_debug("Done Writing to memory\n");
    client->MemoryToWrite = NULL;
    write(socket, "\n\n", 2);
    return true;
}

int64_t EmuNWAccessCoreWrite(SOCKET socket, char ** args, int ac)
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
        send_error(socket, command_error, "No matching Memory Domain name");
        return -1;
    }
    auto client = EmuNWAccessGetClient(socket);
    client->MemoryToWrite = to_read;
    if (ac == 1)
    {
        client->MemoryWriteInfos = (generic_poll_server_memory_argument*)malloc(sizeof(generic_poll_server_memory_argument));
        client->MemoryWriteInfos->next = NULL;
        client->MemoryWriteInfos->offset = 0;
        client->MemoryWriteInfos->size = getMemorySize(args[0]);
        return client->MemoryWriteInfos->size;
    }
    // IF we have a missing size
    if (ac > 3 && ac % 2 != 1)
    {
        send_error(socket, invalid_argument, "You can't only ommit the size of a single write");
        return -1;
    }
    generic_poll_server_memory_argument* pargs = generic_poll_server_parse_memory_argument(args + 1, ac - 1);
    generic_poll_server_memory_argument* cur = pargs;
    // Checking arguments and fixing size when missing
    uint32_t total_size = 0;
    while (cur != NULL)
    {
        if (cur->size == 0)
        {
            if (ac == 2)
            {
                cur->size = getMemorySize(args[0]) - cur->offset;
            } else {
                send_error(socket, invalid_argument, "0 size is invalid");
                generic_poll_server_free_memory_argument(pargs);
                return -1;
            }
        }
        total_size += cur->size;
        cur = cur->next;
    }
    client->MemoryWriteInfos = pargs;
    return total_size;
}

int64_t EmuNWAccessNop(SOCKET socket, char ** args, int ac)
{
    return false;
}

int64_t EmuNWAccessLoadState(SOCKET socket, char ** args, int ac)
{
    NetworkAccessData.stateTriggerMutex = true;
    NetworkAccessData.saveStatePath = (char*)malloc(strlen(args[0]) + 1);
    NetworkAccessData.saveState = false;
    NetworkAccessData.stateTriggerClient = socket;
    strcpy(NetworkAccessData.saveStatePath, args[0]);
    return true;
}

int64_t EmuNWAccessSaveState(SOCKET socket, char ** args, int ac)
{
    NetworkAccessData.stateTriggerMutex = true;
    NetworkAccessData.saveStatePath = (char*)malloc(strlen(args[0]) + 1);
    NetworkAccessData.saveState = true;
    NetworkAccessData.stateTriggerClient = socket;
    strcpy(NetworkAccessData.saveStatePath, args[0]);
    return true;
}
