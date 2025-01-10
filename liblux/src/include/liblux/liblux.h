/*
 * luxOS - a unix-like operating system
 * Omar Elghoul, 2024
 * 
 * liblux: Library abstracting kernel-server communication protocols
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#define SERVER_MAX_SIZE         0x8000             // default max msg size is 32 KiB
#define MAX_FILE_PATH           2048

#define SERVER_KERNEL_PATH      "lux:///kernel"     // not a real file, special path
#define SERVER_LUMEN_PATH       "lux:///lumen"      // likewise not a real file

/* these commands are requested by lumen and the servers and fulfilled by the kernel */
#define COMMAND_LOG             0x0000  // output to kernel log
#define COMMAND_SYSINFO         0x0001  // get uptime, mem usage, etc
#define COMMAND_RAND            0x0002  // random number
#define COMMAND_IO              0x0003  // request I/O access
#define COMMAND_PROCESS_IO      0x0004  // get process I/O privileges
#define COMMAND_PROCESS_LIST    0x0005  // get list of processes/threads
#define COMMAND_PROCESS_STATUS  0x0006  // get status of process/thread
#define COMMAND_FRAMEBUFFER     0x0007  // request frame buffer access

#define MAX_GENERAL_COMMAND     0x0007

/* these commands are requested by the kernel for lumen to fulfill syscall requests */
#define COMMAND_STAT            0x8000
#define COMMAND_FSYNC           0x8001
#define COMMAND_MOUNT           0x8002
#define COMMAND_UMOUNT          0x8003
#define COMMAND_OPEN            0x8004
#define COMMAND_READ            0x8005
#define COMMAND_WRITE           0x8006
#define COMMAND_IOCTL           0x8007
#define COMMAND_OPENDIR         0x8008
#define COMMAND_READDIR         0x8009
#define COMMAND_CHMOD           0x800A
#define COMMAND_CHOWN           0x800B
#define COMMAND_LINK            0x800C
#define COMMAND_MKDIR           0x800D
#define COMMAND_UTIME           0x800E

#define COMMAND_EXEC            0x800F
#define COMMAND_CHDIR           0x8010
#define COMMAND_CHROOT          0x8011

#define COMMAND_MMAP            0x8012
#define COMMAND_MSYNC           0x8013
#define COMMAND_UNLINK          0x8014
#define COMMAND_SYMLINK         0x8015
#define COMMAND_READLINK        0x8016

#define MAX_SYSCALL_COMMAND     0x8016

/* these commands are for device drivers */
#define COMMAND_IRQ             0xC000

/* this command is for drivers to notify lumen of their ready status */
#define COMMAND_LUMEN_READY     0x4444  /* magic number kinda? */

#define KPRINT_LEVEL_DEBUG      0
#define KPRINT_LEVEL_WARNING    1
#define KPRINT_LEVEL_ERROR      2
#define KPRINT_LEVEL_PANIC      3

typedef struct {
    uint16_t command;
    uint64_t length;
    uint8_t response;       // 0 for requests, 1 for response
    uint8_t reserved[3];    // for alignment
    uint64_t latency;       // in ms, for responses
    uint64_t status;        // return value for responses
    pid_t requester;
} MessageHeader;

typedef struct {
    MessageHeader header;
    uint16_t id;            // syscall request ID
} SyscallHeader;

/* sysinfo command */
typedef struct {
    MessageHeader header;
    uint64_t uptime;
    int maxPid, maxSockets, maxFiles;
    int processes, threads;
    int pageSize;
    int memorySize, memoryUsage;    // in pages
    char kernel[64];                // version string
    char cpu[64];                   // CPU model
} SysInfoResponse;

/* log command */
typedef struct {
    MessageHeader header;
    int level;
    char server[512];       // null terminated
    char message[];         // variable length, null terminated
} LogCommand;

/* rand command */
typedef struct {
    MessageHeader header;
    uint64_t number;        // random number
} RandCommand;

/* framebuffer access command */
typedef struct {
    MessageHeader header;
    uint64_t buffer;        // pointer
    uint64_t bufferPhysical;
    uint16_t w, h, pitch, bpp;
} FramebufferResponse;

/* mount command */
typedef struct {
    SyscallHeader header;
    char source[MAX_FILE_PATH];
    char target[MAX_FILE_PATH];
    char type[32];
    int flags;
} MountCommand;

/* stat() */
typedef struct {
    SyscallHeader header;
    char source[MAX_FILE_PATH];
    char path[MAX_FILE_PATH];
    struct stat buffer;
} StatCommand;

/* fsync() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uint64_t id;
    uid_t uid;
    gid_t gid;
} FsyncCommand;

/* open() */
typedef struct {
    SyscallHeader header;
    char abspath[MAX_FILE_PATH];    // absolute path
    char path[MAX_FILE_PATH];       // resolved path relative to dev mountpoint
    char device[MAX_FILE_PATH];     // device
    int flags;
    mode_t mode;
    mode_t umask;
    uid_t uid;
    gid_t gid;
    uint64_t id;    // unique ID
    int charDev;
} OpenCommand;

/* read() and write() */
typedef struct {
    SyscallHeader header;
    int silent;         // request no response
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uint64_t id;
    int flags;
    uid_t uid;
    gid_t gid;
    off_t position;
    size_t length;
    uint64_t data[];    // for alignment
} RWCommand;

/* IRQ Notification */
typedef struct {
    MessageHeader header;
    uint64_t pin;
} IRQCommand;

/* ioctl() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uint64_t id;
    int flags;
    uid_t uid;
    gid_t gid;
    unsigned long opcode;
    unsigned long parameter;
} IOCTLCommand;

/* opendir() */
typedef struct {
    SyscallHeader header;
    char abspath[MAX_FILE_PATH];
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
} OpendirCommand;

/* readdir() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    size_t position;
    int end;            // set to 1 if trying to read beyond max position
    struct dirent entry;
    char data[MAX_FILE_PATH];
} ReaddirCommand;

/* chmod() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
    mode_t mode;
} ChmodCommand;

/* chown() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
    uid_t newUid;
    gid_t newGid;
} ChownCommand;

/* link() and symlink() */
typedef struct {
    SyscallHeader header;
    char oldPath[MAX_FILE_PATH];
    char newPath[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
} LinkCommand;

/* unlink() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
} UnlinkCommand;

/* readlink() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
} ReadLinkCommand;

/* mkdir() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
    mode_t mode;
    mode_t umask;
} MkdirCommand;

/* utime() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
    time_t accessTime;
    time_t modifiedTime;
} UtimeCommand;

/* exec() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;

    uint8_t elf[];      // ELF file
} ExecCommand;

/* chdir() */
typedef struct {
    SyscallHeader header;
    char path[MAX_FILE_PATH];
    uid_t uid;
    gid_t gid;
} ChdirCommand;

/* mmap() */
typedef struct {
    SyscallHeader header;

    /* file descriptor */
    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uint64_t id;
    int openFlags;
    uid_t uid;
    gid_t gid;
    off_t position;

    /* mmap params */
    size_t len;
    int prot;
    int flags;
    off_t off;

    int responseType;   // 0 = returning data, 1 = returning mmio
    uint64_t mmio;      // mmio pointer
    uint64_t data[];
} MmapCommand;

/* msync() */
typedef struct {
    SyscallHeader header;

    char path[MAX_FILE_PATH];
    char device[MAX_FILE_PATH];
    uint64_t id;
    uid_t uid;
    gid_t gid;

    size_t len;
    off_t off;
    int mapFlags;
    int syncFlags;

    uint64_t data[];
} MsyncCommand;

/* wrapper functions */
pid_t luxGetSelf();
const char *luxGetName();
int luxInit(const char *);
int luxInitLumen();
int luxReady();
int luxConnectKernel();
int luxConnectLumen();
int luxConnectDependency(const char *);
int luxGetKernelSocket();
ssize_t luxSendKernel(void *);
ssize_t luxRecvKernel(void *, size_t, bool, bool);
ssize_t luxSendLumen(void *);
ssize_t luxRecvLumen(void *, size_t, bool, bool);
ssize_t luxSendDependency(void *);
ssize_t luxRecvDependency(void *, size_t, bool, bool);
int luxAccept();
int luxAcceptAddr(struct sockaddr *, socklen_t *);
ssize_t luxSend(int, void *);
ssize_t luxRecv(int, void *, size_t, bool, bool);
ssize_t luxRecvCommand(void **);
void luxLog(int, const char *);
void luxLogf(int, const char *, ...);
int luxRequestFramebuffer(FramebufferResponse *);
int luxRequestRNG(uint64_t *);
int luxSysinfo(SysInfoResponse *);