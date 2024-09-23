/*
 * luxOS - a unix-like operating system
 * Omar Elghoul, 2024
 * 
 * lfb: Abstraction for linear frame buffers under /dev/lfb
 */

#include <liblux/liblux.h>
#include <liblux/devfs.h>
#include <liblux/lfb.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static FramebufferResponse fb;
static void *buffer;
static size_t pitch, size;

/* scanline(): returns the scan line containing a given offset
 * params: offset - offset into the frame buffer file
 * returns: scan line, -1 if out of bounds */

off_t scanline(off_t offset) {
    if(offset < 0 || offset > size) return -1;

    return offset / pitch;
}

/* copyLine(): copies a line from the back buffer to the frame buffer
 * params: line - line number
 * returns: nothing
 */

void copyLine(off_t line) {
    const void *src = (void *)((uintptr_t)buffer + (line * pitch));
    void *dst = (void *)((uintptr_t)fb.buffer + (line * fb.pitch));
    memcpy(dst, src, pitch);
}

int main() {
    luxInit("lfb");
    while(luxConnectDependency("devfs"));   // depend on /dev

    // request the frame buffer from the kernel
    if(luxRequestFramebuffer(&fb)) {
        luxLogf(KPRINT_LEVEL_ERROR, "failed to acquire from kernel\n");
        return -1;
    }

    luxLogf(KPRINT_LEVEL_DEBUG, "screen resolution is %dx%d (%d bpp)\n", fb.w, fb.h, fb.bpp);

    pitch = fb.w * fb.bpp / 8;  // abstract whatever pitch the hardware is using
    size = pitch * fb.h;

    // create a character device on /dev for the frame buffer
    DevfsRegisterCommand *regcmd = calloc(1, sizeof(DevfsRegisterCommand));
    struct stat *status = calloc(1, sizeof(struct stat));
    buffer = malloc(size);      // back buffer
    RWCommand *cmd = calloc(1, size + sizeof(RWCommand));

    if(!regcmd || !status || !buffer || !cmd) {
        luxLogf(KPRINT_LEVEL_ERROR, "failed to allocate memory for frame buffer device\n");
        return -1;
    }

    // character device with permissions rw-rw-r--
    status->st_mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IFCHR);
    status->st_size = size;

    // register the device
    regcmd->header.command = COMMAND_DEVFS_REGISTER;
    regcmd->header.length = sizeof(DevfsRegisterCommand);
    strcpy(regcmd->path, "/lfb0");
    strcpy(regcmd->server, "lux:///dslfb");  // server name prefixed with "lux:///ds"
    memcpy(&regcmd->status, status, sizeof(struct stat));
    luxSendDependency(regcmd);

    free(status);
    free(regcmd);

    // notify lumen that startup is complete
    luxReady();

    for(;;) {
        // receive r/w requests from the devfs server
        ssize_t s = luxRecvDependency(cmd, size+sizeof(RWCommand), false, false);
        if(s > 0) {
            if(cmd->header.header.command == COMMAND_WRITE) {
                // writing to the frame buffer
                cmd->header.header.length = sizeof(RWCommand);
                cmd->header.header.response = 1;

                off_t line = scanline(cmd->position);
                off_t lineCount = (cmd->length + pitch - 1) / pitch;
                if(!lineCount) lineCount++;
                if(cmd->position % pitch) lineCount++;

                if(line < 0) {
                    cmd->header.header.status = -EOVERFLOW;
                } else {
                    void *ptr = (void *)((uintptr_t)buffer + cmd->position);
                    memcpy(ptr, cmd->data, cmd->length);
                    
                    for(off_t i = 0; i < lineCount; i++) copyLine(line+i);

                    cmd->header.header.status = cmd->length;
                    cmd->position += cmd->length;
                }

                luxSendDependency(cmd);
            } else if(cmd->header.header.command == COMMAND_READ) {
                // reading from the frame buffer
                // we use a back buffer to avoid slow reading from video RAM
                cmd->header.header.response = 1;
                cmd->header.header.length = sizeof(RWCommand);
                cmd->length = 0;

                if(cmd->position >= size) cmd->header.header.status = -EOVERFLOW;
                else {
                    off_t truelen;
                    if((cmd->position+cmd->length) > size) truelen = size - cmd->position;
                    else truelen = cmd->length;

                    cmd->header.header.length += truelen;
                    cmd->length += truelen;

                    memcpy(cmd->data, (void *)((uintptr_t)buffer+cmd->position), truelen);
                    cmd->position += truelen;
                }

                luxSendDependency(cmd);
            } else if(cmd->header.header.command == COMMAND_IOCTL) {
                // ioctl()
                IOCTLCommand *ioctlcmd = (IOCTLCommand *) cmd;
                ioctlcmd->header.header.response = 1;
                ioctlcmd->header.header.length = sizeof(IOCTLCommand);
                ioctlcmd->header.header.status = 0;
                switch(ioctlcmd->opcode) {
                case LFB_GET_WIDTH:
                    ioctlcmd->parameter = fb.w;
                    break;
                case LFB_GET_HEIGHT:
                    ioctlcmd->parameter = fb.h;
                    break;
                default:
                    ioctlcmd->header.header.status = -ENOTTY;
                }

                luxSendDependency(ioctlcmd);
            } else {
                luxLogf(KPRINT_LEVEL_WARNING, "unimplemented command 0x%X, dropping message...\n", cmd->header.header.command);
            }
        }
    }
}