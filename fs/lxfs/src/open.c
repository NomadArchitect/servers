/*
 * luxOS - a unix-like operating system
 * Omar Elghoul, 2024
 * 
 * lxfs: Driver for the lxfs file system
 */

#include <liblux/liblux.h>
#include <lxfs/lxfs.h>
#include <vfs.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* lxfsOpen(): opens a opened file on an lxfs volume
 * params: ocmd - open command message
 * returns: nothing, response relayed to vfs
 */

void lxfsOpen(OpenCommand *ocmd) {
    ocmd->header.header.response = 1;
    ocmd->header.header.length = sizeof(OpenCommand);

    Mountpoint *mp = findMP(ocmd->device);
    if(!mp) {
        ocmd->header.header.status = -EIO;  // device doesn't exist
        luxSendKernel(ocmd);
        return;
    }

    LXFSDirectoryEntry entry;
    if(!lxfsFind(&entry, mp, ocmd->path, NULL, NULL)) {
        // file doesn't exist, check if it should be created
        if(ocmd->flags & O_CREAT) {
            // no idea why this kinda masking is necessary but POSIX says so lol
            // https://pubs.opengroup.org/onlinepubs/9799919799/functions/open.html
            mode_t mode = ocmd->mode & ~ocmd->umask;
            mode |= S_IFREG;

            ocmd->header.header.status = 0;
            if((ocmd->flags & O_RDONLY) && !(mode & S_IRUSR))
                ocmd->header.header.status = -EACCES;
            if((ocmd->flags & O_WRONLY) && !(mode & S_IWUSR))
                ocmd->header.header.status = -EACCES;

            if(ocmd->header.header.status) {
                luxSendKernel(ocmd);
                return;
            }

            entry.block = 0;
            ocmd->header.header.status = lxfsCreate(&entry, mp, ocmd->path, mode, ocmd->uid, ocmd->gid);
            luxSendKernel(ocmd);
            return;
        }

        ocmd->header.header.status = -ENOENT;
        luxSendKernel(ocmd);
        return;
    }

    // ensure this is a file
    uint8_t type = (entry.flags >> LXFS_DIR_TYPE_SHIFT) & LXFS_DIR_TYPE_MASK;
    if(type == LXFS_DIR_TYPE_DIR) {
        ocmd->header.header.status = -EISDIR;
        luxSendKernel(ocmd);
        return;
    }

    // file exists, ensure O_CREATE | O_EXCL are not set
    if((ocmd->flags & O_CREAT) && (ocmd->flags & O_EXCL)) {
        ocmd->header.header.status = -EEXIST;
        luxSendKernel(ocmd);
        return;
    }

    // delete file contents for O_TRUNC
    if(ocmd->flags & O_TRUNC) {
        if(lxfsReadBlock(mp, entry.block, mp->meta)) {
            ocmd->header.header.status = -EIO;
            luxSendKernel(ocmd);
            return;
        }

        LXFSFileHeader *meta = (LXFSFileHeader *) mp->meta;
        meta->size = 0;
        if(lxfsWriteBlock(mp, entry.block, mp->meta)) {
            ocmd->header.header.status = -EIO;
            luxSendKernel(ocmd);
            return;
        }

        uint64_t next = entry.block;
        while(next != LXFS_BLOCK_EOF) {
            int s;
            if(next == entry.block) s = lxfsSetNextBlock(mp, next, LXFS_BLOCK_EOF);
            else s = lxfsSetNextBlock(mp, next, 0);

            if(s) {
                ocmd->header.header.status = -EIO;
                luxSendKernel(ocmd);
                return;
            }

            next = lxfsNextBlock(mp, next);
            if(!next) {
                ocmd->header.header.status = -EIO;
                luxSendKernel(ocmd);
                return;
            }
        }
    }

    // recursively redirect for soft links
    if(type == LXFS_DIR_TYPE_SOFT_LINK) {
        if(lxfsReadBlock(mp, entry.block, mp->meta)) {
            ocmd->header.header.status = -EIO;
            luxSendKernel(ocmd);
            return;
        }

        memset(ocmd->path, 0, sizeof(ocmd->path));
        memcpy(ocmd->path, mp->meta, entry.size);
        if(ocmd->path[0] == '/') {
            memmove(ocmd->path, ocmd->path+1, entry.size-1);
            ocmd->path[entry.size-1] = 0;
        }

        strcpy(ocmd->abspath+1, ocmd->path);
        ocmd->abspath[0] = '/';

        lxfsOpen(ocmd);
        return;
    }

    // for hard links and regular files proceed as usual
    ocmd->header.header.status = 0;
    if(ocmd->uid == entry.owner) {
        if((ocmd->flags & O_RDONLY) && !(entry.permissions & LXFS_PERMS_OWNER_R)) ocmd->header.header.status = -EACCES;
        if((ocmd->flags & O_WRONLY) && !(entry.permissions & LXFS_PERMS_OWNER_W)) ocmd->header.header.status = -EACCES;
    } else if(ocmd->gid == entry.group) {
        if((ocmd->flags & O_RDONLY) && !(entry.permissions & LXFS_PERMS_GROUP_R)) ocmd->header.header.status = -EACCES;
        if((ocmd->flags & O_WRONLY) && !(entry.permissions & LXFS_PERMS_GROUP_W)) ocmd->header.header.status = -EACCES;
    } else {
        if((ocmd->flags & O_RDONLY) && !(entry.permissions & LXFS_PERMS_OTHER_R)) ocmd->header.header.status = -EACCES;
        if((ocmd->flags & O_WRONLY) && !(entry.permissions & LXFS_PERMS_OTHER_W)) ocmd->header.header.status = -EACCES;
    }

    luxSendKernel(ocmd);
}