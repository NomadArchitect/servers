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
#include <stdarg.h>
#include <errno.h>
#include <time.h>

/* lxfsCreate(): creates a file or directory on the lxfs volume
 * params: dest - destination buffer to store directory entry
 * non-zero block in the dest structure indicates hard link creation
 * params: mp - mountpoint
 * params: path - path to create
 * params: mode - file mode bits
 * params: uid - user ID of the owner
 * params: gid - group ID of the file
 * params: optional path to link target for symbolic links
 * returns: zero on success, negative errno error code on fail
 */

int lxfsCreate(LXFSDirectoryEntry *dest, Mountpoint *mp, const char *path,
               mode_t mode, uid_t uid, gid_t gid, ...) {
    uint64_t hardLink = dest->block;
    const char *symlink = NULL;

    if(S_ISLNK(mode)) {
        va_list args;
        va_start(args, gid);
        symlink = va_arg(args, const char *);
        va_end(args);
    }

    // get the parent directory
    LXFSDirectoryEntry parent;
    int depth = pathDepth(path);
    if(depth <= 1) {
        if(!lxfsFind(&parent, mp, "/", NULL, NULL)) return -EIO;
    } else {
        char *parentPath = strdup(path);
        if(!parentPath) return -1 * errno;
        char *last = strrchr(parentPath, '/');
        if(!last) {
            free(parentPath);
            return -ENOENT;
        }

        *last = 0;  // truncate to keep only the parent directory

        if(!lxfsFind(&parent, mp, parentPath, NULL, NULL)) {
            free(parentPath);
            return -ENOENT;
        }

        free(parentPath);
    }

    if(!pathComponent((char *) dest->name, path, depth-1))
        return -ENOENT;

    // ensure the parent is a directory
    if(((parent.flags >> LXFS_DIR_TYPE_SHIFT) & LXFS_DIR_TYPE_MASK) != LXFS_DIR_TYPE_DIR)
        return -ENOTDIR;
    
    // and that we have write permission
    if(uid == parent.owner) {
        if(!(parent.permissions & LXFS_PERMS_OWNER_W)) return -EACCES;
    } else if(gid == parent.group) {
        if(!(parent.permissions & LXFS_PERMS_GROUP_W)) return -EACCES;
    } else {
        if(!(parent.permissions & LXFS_PERMS_OTHER_W)) return -EACCES;
    }

    dest->entrySize = sizeof(LXFSDirectoryEntry) + strlen((const char *) dest->name) - 511;
    dest->flags = LXFS_DIR_VALID;
    if(hardLink) dest->flags |= (LXFS_DIR_TYPE_HARD_LINK << LXFS_DIR_TYPE_SHIFT);
    else if(S_ISREG(mode)) dest->flags |= (LXFS_DIR_TYPE_FILE << LXFS_DIR_TYPE_SHIFT);
    else if(S_ISLNK(mode)) dest->flags |= (LXFS_DIR_TYPE_SOFT_LINK << LXFS_DIR_TYPE_SHIFT);
    else if(S_ISDIR(mode)) dest->flags |= (LXFS_DIR_TYPE_DIR << LXFS_DIR_TYPE_SHIFT);
    
    if(mode & S_IRUSR) dest->permissions |= LXFS_PERMS_OWNER_R;
    if(mode & S_IWUSR) dest->permissions |= LXFS_PERMS_OWNER_W;
    if(mode & S_IXUSR) dest->permissions |= LXFS_PERMS_OWNER_X;
    if(mode & S_IRGRP) dest->permissions |= LXFS_PERMS_GROUP_R;
    if(mode & S_IWGRP) dest->permissions |= LXFS_PERMS_GROUP_W;
    if(mode & S_IXGRP) dest->permissions |= LXFS_PERMS_GROUP_X;
    if(mode & S_IROTH) dest->permissions |= LXFS_PERMS_OTHER_R;
    if(mode & S_IWOTH) dest->permissions |= LXFS_PERMS_OTHER_W;
    if(mode & S_IXOTH) dest->permissions |= LXFS_PERMS_OTHER_X;

    dest->size = 0;
    dest->owner = uid;
    dest->group = gid;
    
    time_t timestamp = time(NULL);
    dest->accessTime = timestamp;
    dest->createTime = timestamp;
    dest->modTime = timestamp;

    memset(dest->reserved, 0, sizeof(dest->reserved));

    if(!hardLink) {
        dest->block = lxfsFindFreeBlock(mp, 0);
        if(!dest->block) return -ENOSPC;
        if(lxfsSetNextBlock(mp, dest->block, LXFS_BLOCK_EOF))
            return -EIO;

        memset(mp->dataBuffer, 0, mp->blockSizeBytes);
        
        if(S_ISREG(mode)) {
            LXFSFileHeader *fileHeader = (LXFSFileHeader *) mp->dataBuffer;
            fileHeader->refCount = 1;
            fileHeader->size = 0;
            if(lxfsWriteBlock(mp, dest->block, mp->dataBuffer)) {
                lxfsSetNextBlock(mp, dest->block, LXFS_BLOCK_FREE);
                return -EIO;
            }
        } else if(S_ISDIR(mode)) {
            LXFSDirectoryHeader *dirHeader = (LXFSDirectoryHeader *) mp->dataBuffer;
            dirHeader->accessTime = timestamp;
            dirHeader->createTime = timestamp;
            dirHeader->modTime = timestamp;
            dirHeader->reserved = 0;
            dirHeader->sizeBytes = sizeof(LXFSDirectoryHeader);
            dirHeader->sizeEntries = 0;
            if(lxfsWriteBlock(mp, dest->block, mp->dataBuffer)) {
                lxfsSetNextBlock(mp, dest->block, LXFS_BLOCK_FREE);
                return -EIO;
            }
        } else if(S_ISLNK(mode)) {
            strcpy((char *) mp->dataBuffer, symlink);
            if(lxfsWriteBlock(mp, dest->block, mp->dataBuffer)) {
                lxfsSetNextBlock(mp, dest->block, LXFS_BLOCK_FREE);
                return -EIO;
            }

            dest->size = strlen(symlink);
        }
    } else {
        if(lxfsReadBlock(mp, dest->block, mp->dataBuffer)) return -EIO;
        LXFSFileHeader *fileHeader = (LXFSFileHeader *) mp->dataBuffer;
        fileHeader->refCount++;
        dest->size = fileHeader->size;
        if(lxfsWriteBlock(mp, dest->block, mp->dataBuffer)) return -EIO;
    }

    lxfsFlushBlock(mp, dest->block);
    uint64_t block = parent.block;
    uint64_t prevBlock;

    LXFSDirectoryHeader *parentHeader = (LXFSDirectoryHeader *) mp->dataBuffer;
    LXFSDirectoryEntry *dir = (LXFSDirectoryEntry *)((uintptr_t) mp->dataBuffer + sizeof(LXFSDirectoryHeader));
    off_t offset = sizeof(LXFSDirectoryHeader);

    for(;;) {
        prevBlock = block;
        block = lxfsReadNextBlock(mp, block, mp->dataBuffer);
        if(!block) return -EIO;

        if(block != LXFS_BLOCK_EOF)
            lxfsReadBlock(mp, block, mp->dataBuffer + mp->blockSizeBytes);
        
        while(offset < mp->blockSizeBytes) {
            if(!dir->flags & LXFS_DIR_VALID && (!dir->entrySize || (dir->entrySize >= dest->entrySize)))
                break;
            offset += dir->entrySize;
            dir = (LXFSDirectoryEntry *)((uintptr_t) dir + dir->entrySize);
        }

        if(!dir->flags & LXFS_DIR_VALID && (!dir->entrySize || (dir->entrySize >= dest->entrySize))) {
            if(offset + dest->entrySize <= mp->blockSizeBytes) {
                // directory entry doesn't cross block boundary
                memcpy(dir, dest, dest->entrySize);
                if(lxfsWriteBlock(mp, prevBlock, mp->dataBuffer)) {
                    if(!hardLink) lxfsSetNextBlock(mp, dest->block, LXFS_BLOCK_FREE);
                    return -EIO;
                }
                lxfsFlushBlock(mp, prevBlock);
            } else {
                // free entry but it crosses a block boundary, so allocate one more block
                block = lxfsFindFreeBlock(mp, 0);
                if(!block) return -ENOSPC;
                if(lxfsSetNextBlock(mp, prevBlock, block)) return -EIO;
                if(lxfsSetNextBlock(mp, block, LXFS_BLOCK_EOF)) {
                    lxfsSetNextBlock(mp, prevBlock, LXFS_BLOCK_EOF);
                    return -EIO;
                }

                memset(mp->dataBuffer + mp->blockSizeBytes, 0, mp->blockSizeBytes);
                memcpy(dir, dest, dest->entrySize);
                if(lxfsWriteBlock(mp, prevBlock, mp->dataBuffer))
                    return -EIO;
                if(lxfsWriteBlock(mp, block, mp->dataBuffer + mp->blockSizeBytes))
                    return -EIO;
                
                lxfsFlushBlock(mp, prevBlock);
                lxfsFlushBlock(mp, block);
            }

            // TODO: is there a better way to handle errors here?
            // I'd argue this is a forgiveable error for lack of a better word
            // and the POSIX spec doesn't cover this afaik
            if(lxfsReadBlock(mp, parent.block, mp->dataBuffer))
                return 0;

            parentHeader->sizeBytes += dest->entrySize;
            parentHeader->sizeEntries++;
            parentHeader->accessTime = timestamp;
            parentHeader->modTime = timestamp;
            lxfsWriteBlock(mp, parent.block, mp->dataBuffer);
            lxfsFlushBlock(mp, parent.block);
            return 0;
        }

        if(block == LXFS_BLOCK_EOF) {
            // TODO:
            // reached last block without finding a free entry, allocate a new block here too
            luxLogf(KPRINT_LEVEL_ERROR, "TODO: %s: %d\n", __FILE__, __LINE__);
            return -ENOSYS;
        }

        memcpy(mp->dataBuffer + mp->blockSizeBytes, mp->dataBuffer, mp->blockSizeBytes);
        offset -= mp->blockSizeBytes;
    }
}
