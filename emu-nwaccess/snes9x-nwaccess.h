#pragma once
#include <atomic>

#ifdef _WIN32
    #include <winsock2.h>
    typedef uintptr_t THREAD_TYPE;
#endif

#ifdef SNES9X_GTK
    #include <glib.h>
    typedef  GThread* THREAD_TYPE;
#endif

const int NWACCESS_START_PORT = 68500;
const int NWACCESS_MAX_CLIENTS = 5;

enum class NetworkAccessControlCommand {
    CMD_RESET,
    CMD_PAUSE,
    CMD_STOP,
    CMD_RESUME,
    CMD_RELOAD,
    CMD_LOAD
};

#include "emulator-networkaccess/generic poll server/generic_poll_server.h"

struct NetworkAccessClient {
    SOCKET      socket;
    bool        used = false;
    char        id[512];
    bool        connected;
    
    uint8*      MemoryToWrite;
    generic_poll_server_memory_argument* MemoryWriteInfos;
};

struct NetworkAccessInfos {
    bool            initalized = false;
    SOCKET          serverSocket;
    unsigned int    listenPort;
    struct NetworkAccessClient	clients[5];
    
    std::atomic_bool stateTriggerMutex;
    std::atomic_bool stateDoneMutex;
    std::atomic_bool controlCommandTriggerMutex;
    std::atomic_bool controlCommandDone;
    
    SOCKET          stateTriggerClient;
    char*           saveStatePath;
    bool            stateError;
    bool            saveState;

    bool                        controlCommandError;
    NetworkAccessControlCommand command;
    char*                       romToLoad;
    SOCKET                      controlClient;
    bool                        controlError;

    std::atomic_bool    stopRequest;
    std::atomic_bool    messageMutex;
    char            message[512];
    THREAD_TYPE      thread;
    char*           id;
};

bool    S9xNWAccessInit();
bool	S9xNWAccessStart();
bool	S9xNWAccessStop();
void	S9XSetNWAError(const char*msg);
bool    S9xNWAGuiLoop();
void    S9xNWAServerLoop(void*);
void    EmuNWAccessSetMessage(char* msg);
bool    EmuNWAccessAfterPoll();

bool    EmuNWAccessServerStarted(int port);

struct NetworkAccessClient* EmuNWAccessGetClient(SOCKET socket);


// This is needed by the generic_poll_server code from the Emu Network Access project

bool    EmuNWAccessWriteToMemory(SOCKET socket, char* data, uint32_t);
void    EmuNWAccessNewClient(SOCKET socket);
void    EmuNWAccessRemoveClient(SOCKET socket);

int64_t    EmuNWAccessSetClientName(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessEmuInfo(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessEmuStatus(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessEmuPause(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessEmuStop(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessEmuReset(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessEmuResume(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessEmuReload(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessLoadGame(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessGameInfo(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessCoreList(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessCoreInfo(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessCoreCurrentInfo(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessCoreLoad(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessCoreMemories(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessCoreRead(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessCoreWrite(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessNop(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessLoadState(SOCKET socket, char **args, int ac);
int64_t    EmuNWAccessSaveState(SOCKET socket, char **args, int ac);
