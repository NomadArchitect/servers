/*
 * luxOS - a unix-like operating system
 * Omar Elghoul, 2024
 * 
 * pty: Microkernel server implementing Unix 98-style pseudo-terminal devices
 */

/* Note to self:
 *
 * The primary pseudo-terminal multiplexer is located at /dev/ptmx, and secondary
 * pseudo-terminals are at /dev/ptsX. Primary pseudo-terminals do not have a
 * file system representation, and they are only accessed through their file
 * descriptors. Every time an open() syscall opens /dev/ptmx, a new primary-
 * secondary pseudo-terminal pair is created, the file descriptor of the primary is
 * returned, and the secondary is created in /dev/ptsX. The primary can find the
 * name of the secondary using ptsname(), and the secondary is deleted from the file
 * system after no more processes have an open file descriptor pointing to it.
 * 
 * After the primary is created, the secondary's permissions are adjusted by calling
 * grantpt() with the primary file descriptor. Next, the secondary is unlocked by
 * calling unlockpt() with the primary file descriptor. Finally, the controlling
 * process calls ptsname() to find the secondary's file name and opens it using the
 * standard open(). The opened secondary file descriptor can then be set to the
 * controlling pseudo-terminal of a process using ioctl(). The input/output of
 * the secondary can be read/written through the primary, enabling the controlling
 * process to implement a terminal emulator.
 * 
 * https://unix.stackexchange.com/questions/405972/
 * https://unix.stackexchange.com/questions/117981/
 * https://man7.org/linux/man-pages/man7/pty.7.html
 * https://man7.org/linux/man-pages/man3/grantpt.3.html
 * https://man7.org/linux/man-pages/man3/unlockpt.3.html
 */

#include <liblux/liblux.h>
#include <liblux/devfs.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty/pty.h>

Pty *ptys;
int ptyCount;

int main() {
    luxInit("pty");
    while(luxConnectDependency("devfs"));   // depend on /dev

    // create the primary multiplexer device, /dev/ptmx
    struct stat *status = calloc(1, sizeof(struct stat));
    DevfsRegisterCommand *regcmd = calloc(1, sizeof(DevfsRegisterCommand));
    SyscallHeader *msg = calloc(1, SERVER_MAX_SIZE);
    ptys = calloc(MAX_PTYS, sizeof(Pty));
    ptyCount = 0;

    if(!status || !regcmd || !msg || !ptys) {
        luxLogf(KPRINT_LEVEL_ERROR, "failed to allocate memory for pty server\n");
        return -1;
    }

    // character special file owned by root:root and rw-rw-rw
    // this is following the linux implementation
    status->st_mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH | S_IFCHR);
    status->st_uid = 0;
    status->st_gid = 0;
    status->st_size = 4096;

    // construct the devfs register command
    regcmd->header.command = COMMAND_DEVFS_REGISTER;
    regcmd->header.length = sizeof(DevfsRegisterCommand);
    regcmd->handleOpen = 1;         // we need to handle open() here and override the vfs
    strcpy(regcmd->path, "/ptmx");
    strcpy(regcmd->server, "lux:///dspty");  // server name prefixed with "lux:///ds"
    memcpy(&regcmd->status, status, sizeof(struct stat));
    luxSendDependency(regcmd);

    ssize_t rs = luxRecvDependency(regcmd, regcmd->header.length, true, false);
    if(rs < sizeof(DevfsRegisterCommand) || regcmd->header.status
    || regcmd->header.command != COMMAND_DEVFS_REGISTER) {
        luxLogf(KPRINT_LEVEL_ERROR, "failed to register pty device, error code = %d\n", regcmd->header.status);
        for(;;);
    }

    free(status);
    free(regcmd);

    // notify lumen that this server is ready
    luxReady();

    for(;;) {
        ssize_t s = luxRecvCommand((void **) &msg);
        if(s > 0) {
            switch(msg->header.command) {
            case COMMAND_OPEN: ptyOpen((OpenCommand *) msg); break;
            case COMMAND_IOCTL: ptyIoctl((IOCTLCommand *) msg); break;
            case COMMAND_WRITE: ptyWrite((RWCommand *) msg); break;
            case COMMAND_READ: ptyRead((RWCommand *) msg); break;
            case COMMAND_FSYNC: ptyFsync((FsyncCommand *) msg); break;
            default:
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented command 0x%X, dropping message...\n", msg->header.command);
            }
        } else {
            sched_yield();
        }
    }
}