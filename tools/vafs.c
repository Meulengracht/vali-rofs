/**
 * Copyright 2022, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * VaFs Builder
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

#define FUSE_USE_VERSION 32

#include <assert.h>
#include <errno.h>
#include <fuse3/fuse.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vafs/vafs.h>
#include <vafs/file.h>
#include <vafs/symlink.h>
#include <vafs/directory.h>
#include <vafs/stat.h>

extern int __handle_filter(struct VaFs* vafs);

/** Open a file
 *
 * Open flags are available in fi->flags. The following rules
 * apply.
 *
 *  - Creation (O_CREAT, O_EXCL, O_NOCTTY) flags will be
 *    filtered out / handled by the kernel.
 *
 *  - Access modes (O_RDONLY, O_WRONLY, O_RDWR, O_EXEC, O_SEARCH)
 *    should be used by the filesystem to check if the operation is
 *    permitted.  If the ``-o default_permissions`` mount option is
 *    given, this check is already done by the kernel before calling
 *    open() and may thus be omitted by the filesystem.
 *
 *  - When writeback caching is enabled, the kernel may send
 *    read requests even for files opened with O_WRONLY. The
 *    filesystem should be prepared to handle this.
 *
 *  - When writeback caching is disabled, the filesystem is
 *    expected to properly handle the O_APPEND flag and ensure
 *    that each write is appending to the end of the file.
 * 
     *  - When writeback caching is enabled, the kernel will
 *    handle O_APPEND. However, unless all changes to the file
 *    come through the kernel this will not work reliably. The
 *    filesystem should thus either ignore the O_APPEND flag
 *    (and let the kernel handle it), or return an error
 *    (indicating that reliably O_APPEND is not available).
 *
 * Filesystem may store an arbitrary file handle (pointer,
 * index, etc) in fi->fh, and use this in other all other file
 * operations (read, write, flush, release, fsync).
 *
 * Filesystem may also implement stateless file I/O and not store
 * anything in fi->fh.
 *
 * There are also some flags (direct_io, keep_cache) which the
 * filesystem may set in fi, to change the way the file is opened.
 * See fuse_file_info structure in <fuse_common.h> for more details.
 *
 * If this request is answered with an error code of ENOSYS
 * and FUSE_CAP_NO_OPEN_SUPPORT is set in
 * `fuse_conn_info.capable`, this is treated as success and
 * future calls to open will also succeed without being send
 * to the filesystem process.
 *
 */
int __vafs_open(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct VaFs*           vafs    = context->private_data;
    struct VaFsFileHandle* handle;
    int                    status;

	if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        errno = EACCES;
		return -1;
    }

    status = vafs_file_open(vafs, path, &handle);
    if (status) {
        return status;
    }

    fi->fh = (uint64_t)handle;
    return 0;
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 */
int __vafs_access(const char* path, int permissions)
{
    struct fuse_context* context = fuse_get_context();
    struct VaFs*         vafs    = context->private_data;
    struct vafs_stat     stat;
    int                  status;

    status = vafs_path_stat(vafs, path, 1, &stat);
    if (status) {
        return status;
    }

    if ((stat.mode & (uint32_t)permissions) != (uint32_t)permissions) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.	 An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 */
int __vafs_read(const char* path, char* buffer, size_t count, off_t offset, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct VaFs*           vafs    = context->private_data;
    struct VaFsFileHandle* handle  = (struct VaFsFileHandle*)fi->fh;
    int                    status;

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (offset != 0) {
        status = vafs_file_seek(handle, offset, SEEK_SET);
        if (status) {
            return status;
        }
    }

    status = (int)vafs_file_read(handle, buffer, count);
    if (status) {
        return status;
    }
    return (int)count;
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored. The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given. In that case it is passed to userspace,
 * but libfuse and the kernel will still assign a different
 * inode for internal use (called the "nodeid").
 *
 * `fi` will always be NULL if the file is not currently open, but
 * may also be NULL if the file is open.
 */
int __vafs_getattr(const char* path, struct stat* stat, struct fuse_file_info *fi)
{
    struct fuse_context* context = fuse_get_context();
    struct VaFs*         vafs    = context->private_data;
    struct vafs_stat     vstat;
    int                  status;
    int                  isRoot;

    memset(stat, 0, sizeof(struct stat));
    if (fi != NULL && fi->fh != 0) {
        struct VaFsFileHandle* handle = (struct VaFsFileHandle*)fi->fh;

        stat->st_blksize = 512;
        stat->st_mode    = vafs_file_permissions(handle);
        stat->st_size    = (off_t)vafs_file_length(handle);
        stat->st_nlink   = 1;
        return 0;
    }

    status = vafs_path_stat(vafs, path, 0, &vstat);
    if (status) {
        return status;
    }

    // root has 2 links
    isRoot = (strcmp(path, "/") == 0);

    stat->st_blksize = 512;
    stat->st_mode    = vstat.mode;
    stat->st_size    = (off_t)vstat.size;
    stat->st_nlink   = isRoot + 1;
    return 0;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.	If the linkname is too long to fit in the
 * buffer, it should be truncated.	The return value should be 0
 * for success.
 */
int __vafs_readlink(const char* path, char* linkBuffer, size_t bufferSize)
{
    struct fuse_context*      context = fuse_get_context();
    struct VaFs*              vafs    = context->private_data;
    struct VaFsSymlinkHandle* handle;
    int                       status;

    status = vafs_symlink_open(vafs, path, &handle);
    if (status) {
        return status;
    }

    status = vafs_symlink_target(handle, linkBuffer, bufferSize);
    vafs_symlink_close(handle);
    return status;
}

/**
 * Find next data or hole after the specified offset
 */
off_t __vafs_lseek(const char* path, off_t off, int whence, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct VaFs*           vafs    = context->private_data;
    struct VaFsFileHandle* handle  = (struct VaFsFileHandle*)fi->fh;

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    vafs_file_seek(handle, off, whence);
    return off;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file handle.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 */
int __vafs_release(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct VaFs*           vafs    = context->private_data;
    struct VaFsFileHandle* handle  = (struct VaFsFileHandle*)fi->fh;
    int                    status;

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_file_close(handle);
    fi->fh = 0;
    return status;
}

/** Open directory
 *
 * Unless the 'default_permissions' mount option is given,
 * this method should check if opendir is permitted for this
 * directory. Optionally opendir may also return an arbitrary
 * filehandle in the fuse_file_info structure, which will be
 * passed to readdir, releasedir and fsyncdir.
 */
int __vafs_opendir(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*        context = fuse_get_context();
    struct VaFs*                vafs    = context->private_data;
    struct VaFsDirectoryHandle* handle;
    int                         status;

    status = vafs_directory_open(vafs, path, &handle);
    if (status) {
        return status;
    }

    fi->fh = (uint64_t)handle;
    return 0;
}

/** Read directory
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 */
int __vafs_readdir(
    const char*             path,
    void*                   buffer,
    fuse_fill_dir_t         fill,
    off_t                   offset,
    struct fuse_file_info*  fi,
    enum fuse_readdir_flags flags)
{
    struct fuse_context*        context = fuse_get_context();
    struct VaFs*                vafs    = context->private_data;
    struct VaFsDirectoryHandle* handle  = (struct VaFsDirectoryHandle*)fi->fh;
    int                         status;

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // add ./.. 
	fill(buffer, ".", NULL, 0, 0);
	fill(buffer, "..", NULL, 0, 0);

    while (1) {
        struct VaFsEntry entry;
        //struct stat      stat;
        status = vafs_directory_read(handle, &entry);
        if (status) {
            if (errno != ENOENT) {
                return status;
            }
            break;
        }

        // TODO support retrieving file attributes from a directory entry
        /*if (flags & FUSE_READDIR_PLUS) {
            status = fill(buffer, entry.Name, &stat, 0, FUSE_FILL_DIR_PLUS);
            if (status) {
                return status;
            }
        } else { */
            status = fill(buffer, entry.Name, NULL, 0, 0);
            if (status) {
                return status;
            }
        //}
    }

    return 0;
}

/** Release directory
 */
int __vafs_releasedir(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*        context = fuse_get_context();
    struct VaFs*                vafs    = context->private_data;
    struct VaFsDirectoryHandle* handle  = (struct VaFsDirectoryHandle*)fi->fh;
    int                         status;

    if (handle == NULL) {
        errno = EINVAL;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;

        return -1;
    }

    status = vafs_directory_close(handle);
    fi->fh = 0;
    return status;
}

/** Get file system statistics
 *
 * The 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 */
int __vafs_statfs(const char* path, struct statvfs* stat)
{
    struct fuse_context* context = fuse_get_context();
    struct VaFs*         vafs    = context->private_data;

    // not used
    (void)path;

    stat->f_bsize   = 512; // block size? how are we doing this?
    stat->f_frsize  = 512; // fragment size, dunno
    stat->f_blocks  = 0; // eh, we have no way of determining this atm
    stat->f_bfree   = 0; // Always zero
    stat->f_bavail  = 0; // Always zero
    stat->f_ffree   = 0; // Always zero
    stat->f_favail  = 0; // Always zero
    stat->f_files   = 0; // ...
    stat->f_fsid    = 0; // ignored?
    stat->f_flag    = ST_RDONLY; // ignored?
    stat->f_namemax = 255;
    return 0;
}

/**
 * All methods are optional, but some are essential for a useful
 * filesystem (e.g. getattr).  Open, flush, release, fsync, opendir,
 * releasedir, fsyncdir, access, create, truncate, lock, init and
 * destroy are special purpose methods, without which a full featured
 * filesystem can still be implemented.
 */
static const struct fuse_operations operations = {
    .open       = __vafs_open,
    .access     = __vafs_access,
    .read       = __vafs_read,
    .lseek      = __vafs_lseek,
    .getattr    = __vafs_getattr,
    .readlink   = __vafs_readlink,
    .release    = __vafs_release,
    .opendir    = __vafs_opendir,
    .readdir    = __vafs_readdir,
    .releasedir = __vafs_releasedir,
    .statfs     = __vafs_statfs,
};

/**
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
	const char* filename;
	int         show_help;
} g_options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt g_optionsSpec[] = {
	OPTION("--image=%s", filename),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static void __show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("File-system specific options:\n"
	       "    --image=<s>         Name of the \"VaFS\" disk image\n"
	       "                        (default: \"image.vafs\")\n"
	       "\n");
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct VaFs*     vafs = NULL;
    int              status;

    // Set command line defaults before parsing them.
	g_options.filename = strdup("./image.vafs");

    status = fuse_opt_parse(&args, &g_options, g_optionsSpec, NULL);
	if (status) {
        return status;
    }

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (g_options.show_help) {
		__show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
        goto run_main;
	}

    status = vafs_open_file(g_options.filename, &vafs);
    if (status != 0) {
        fprintf(stderr, "failed to open %s\n", g_options.filename);
		__show_help(argv[0]);
        return -1;
    }

    status = __handle_filter(vafs);
    if (status) {
        fprintf(stderr, "failed to set decode filter for vafs image\n");
        return -1;
    }

run_main:
	status = fuse_main(argc, argv, &operations, vafs);
    if (vafs != NULL) {
        vafs_close(vafs);
    }
    fuse_opt_free_args(&args);
    return status;
}
