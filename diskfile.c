#include "diskfile.h"

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#if FUSE_VERSION < FUSE_MAKE_VERSION(2, 8)
#error Ancient fuse version
#endif

#define FOREACH_ENTRY(_var) \
	for (diskfile_entry *_var = diskfile_entries; \
		_var < diskfile_entries + diskfile_entries_count; \
		++_var)

diskfile_entry diskfile_entries[DISKFILE_MAX_ENTRIES];
size_t diskfile_entries_count = 0;
time_t diskfile_time;

static diskfile_fh *get_fi_fh(struct fuse_file_info *fi) {
	return (void *) fi->fh;
}
static void set_fi_fh(struct fuse_file_info *fi, diskfile_fh *fh) {
	fi->fh = (uint64_t) fh;
}

static off_t
diskfile_source_size(const char *path) {
	struct stat st;
	int err = lstat(path, &st);
	if (err != 0 || S_ISFIFO(st.st_mode)) {
		return 0;
	}
	if (S_ISREG(st.st_mode)) {
		return st.st_size;
	}
	return diskfile_device_size(path);
}

static int
diskfile_getattr(const char *path, struct stat *stbuf) {
  memset(stbuf, 0, sizeof(struct stat));

  if (strcmp(path, "/") == 0) { /* The root directory of our file system. */
    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_nlink = 2 + diskfile_entries_count;
    return 0;
  }
	FOREACH_ENTRY(entry) {
		if (strcmp(path, entry->dest) == 0) {
			stbuf->st_mode = S_IFREG | 0444;
			stbuf->st_nlink = 1;

			if (entry->size == -1)
				entry->size = diskfile_source_size(entry->source);
			stbuf->st_size = entry->size;
			stbuf->st_ctime = diskfile_time;
			stbuf->st_mtime = diskfile_time;
			stbuf->st_atime = diskfile_time;
			return 0;
		}
	}
  return -ENOENT;
}

static int
diskfile_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
  if (strcmp(path, "/") != 0) /* We only recognize the root directory. */
    return -ENOENT;

  filler(buf, ".", NULL, 0);           /* Current directory (.)  */
  filler(buf, "..", NULL, 0);          /* Parent directory (..)  */
	FOREACH_ENTRY(entry) {
		filler(buf, entry->dest + 1, NULL, 0);
	}

	return 0;
}

static int diskfile_open(const char *path, struct fuse_file_info *fi) {
	FOREACH_ENTRY(entry) {
		if (strcmp(path, entry->dest) == 0) {
			if ((fi->flags & O_ACCMODE) != O_RDONLY)
				return -EACCES;

			struct stat st;
			if (lstat(entry->source, &st) != 0) {
				return -errno;
			}

			diskfile_fh *fh = malloc(sizeof(diskfile_fh));
			if (fh == NULL) {
				return -errno;
			}

			fh->nonseekable = (S_ISFIFO(st.st_mode)) ? 1 : 0;
			fi->nonseekable = fi->direct_io = fh->nonseekable;
			// Looks like setting direct_io too is a good idea  https://github.com/libfuse/libfuse/blob/fuse-2.9.9/example/fsel.c#L112
			// (Also from that example, improvement: return -EBUSY to prevent pipe being open more than once)

			fh->fd = open(entry->source, O_RDONLY);
			if (fh->fd == -1) {
				free(fh);
				return -errno;
			}

			// https://github.com/libfuse/libfuse/wiki/FAQ#is-it-possible-to-store-a-pointer-to-private-data-in-the-fuse_file_info-structure
			set_fi_fh(fi, fh);
			return 0;
		}
	}
	return -ENOENT;
}

static int
diskfile_release(const char *path, struct fuse_file_info *fi) {
	int err = close(get_fi_fh(fi)->fd);
	free(get_fi_fh(fi));
	return (err == 0) ? 0 : -errno;
}

static int
diskfile_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
	int read_total = 0;
	int nonseekable = get_fi_fh(fi)->nonseekable;
	// Note: fi->nonseekable is always zero here. See "Available in" in comments in fuse_common.h.

	if (nonseekable) {
		/*
		"nonseekable" guarantees that "offset" is correct. Try running:

		A.  tac -r -s 'x\|[^x]' /mnt/specialfile/dm-1 >/dev/null

		B.  tac -r -s 'x\|[^x]' /mnt/specialfile/p >/dev/null
				cat /dev/dm-1 >p	# In other terminal

		(tac is reversing char-by-char (see example at the bottom of https://www.gnu.org/software/coreutils/manual/html_node/tac-invocation.html))
		*/

		size_t remaining = size;
		while (1) {
			int read_partial = read(get_fi_fh(fi)->fd, buf, remaining);

			if (read_partial == 0) {				// EOF
				break;
			} else if (read_partial < 0) {	// Error
				read_total = -1;
				break;
			}

			read_total += read_partial;
			buf += read_partial;
			remaining -= read_partial;
			if (remaining <= 0) {
				break;
			}

			debug_fprintf(stderr, "read %d,\t%lu remaining\n", read_partial, remaining);
			// Try this branch running:
			// { echo a; sleep 1; echo b; sleep 1; echo c; sleep 1; } >p
		}

	} else {
		read_total = pread(get_fi_fh(fi)->fd, buf, size, offset);
	}

	debug_fprintf(stderr,
		"nonseekable=%d"
		"\tsize=%lu"
		"\tread_total=%d"
		"\toffset=%ld"
		"\n",
		nonseekable,
		size,
		read_total,
		offset
	);
	return (read_total >= 0) ? read_total : -errno;
}

struct fuse_operations diskfile_operations = {
  .getattr     = diskfile_getattr,
  .readdir     = diskfile_readdir,
  .open        = diskfile_open,
	.release     = diskfile_release,
  .read        = diskfile_read,
};
