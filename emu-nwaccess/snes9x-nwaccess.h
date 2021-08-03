#pragma once


#ifdef _WIN32
    #include <winsock2.h>
#endif


const int NWACCESS_START_PORT = 68500;
const int NWACCESS_MAX_CLIENTS = 5;

struct NetworkAccessClient {
    SOCKET      socket;
    bool        used = false;
    char*       id;
    bool        connected;
    
    uint8*      MemoryToWrite;
    uint32_t    MemorySizeToWrite;
    uint32_t    MemorySizeWritten;
    uint32_t    MemoryOffsetToWrite;
};

struct NetworkAccessInfos {
    bool            initalized = false;
    SOCKET          serverSocket;
    unsigned int    listenPort;
    struct NetworkAccessClient	clients[5];
    HANDLE          messageMutex;
    char            message[512];
    uintptr_t       thread;
    char*           id;
};

bool    S9xNWAccessInit();
bool	S9xNWAccessStart();
bool	S9xNWAccessStop();
void	S9XSetNWAError(const char*msg);
bool    S9xNWAGuiLoop();
void    S9xNWAServerLoop(void*);
void    EmuNWAccessSetMessage(char* msg);

bool    EmuNWAccessServerStarted(int port);

struct NetworkAccessClient* EmuNWAccessGetClient(SOCKET socket);


// This is needed by the generic_poll_server code from the Emu Network Access project

bool    EmuNWAccessWriteToMemory(SOCKET socket, char* data, uint32_t);
void    EmuNWAccessNewClient(SOCKET socket);
void    EmuNWAccessRemoveClient(SOCKET socket);

bool    EmuNWAccessEmuInfo(SOCKET socket, char **args, int ac);
bool    EmuNWAccessEmuStatus(SOCKET socket, char **args, int ac);
bool    EmuNWAccessEmuPause(SOCKET socket, char **args, int ac);
bool    EmuNWAccessEmuStop(SOCKET socket, char **args, int ac);
bool    EmuNWAccessEmuReset(SOCKET socket, char **args, int ac);
bool    EmuNWAccessEmuResume(SOCKET socket, char **args, int ac);
bool    EmuNWAccessEmuReload(SOCKET socket, char **args, int ac);
bool    EmuNWAccessLoadGame(SOCKET socket, char **args, int ac);
bool    EmuNWAccessGameInfo(SOCKET socket, char **args, int ac);
bool    EmuNWAccessCoreList(SOCKET socket, char **args, int ac);
bool    EmuNWAccessCoreInfo(SOCKET socket, char **args, int ac);
bool    EmuNWAccessCoreCurrentInfo(SOCKET socket, char **args, int ac);
bool    EmuNWAccessCoreLoad(SOCKET socket, char **args, int ac);
bool    EmuNWAccessCoreMemories(SOCKET socket, char **args, int ac);
bool    EmuNWAccessCoreRead(SOCKET socket, char **args, int ac);
bool    EmuNWAccessCoreWrite(SOCKET socket, char **args, int ac);
bool    EmuNWAccessNop(SOCKET socket, char **args, int ac);
bool    EmuNWAccessLoadState(SOCKET socket, char **args, int ac);
bool    EmuNWAccessSaveState(SOCKET socket, char **args, int ac);
