#ifndef _DISKFILE_H
#define _DISKFILE_H

#define _GNU_SOURCE // asprintf
#include <sys/types.h>

#define DISKFILE_MAX_ENTRIES 256

typedef struct {
	char *source;
	char *dest;
	off_t size;
} diskfile_entry;
extern diskfile_entry diskfile_entries[];
extern size_t diskfile_entries_count;
extern time_t diskfile_time;

typedef struct {
	int fd;
	int nonseekable;
} diskfile_fh;

extern struct fuse_operations diskfile_operations;
off_t diskfile_device_size(const char *path);

#ifdef DEBUG
	#define debug_fprintf(...) fprintf(__VA_ARGS__)
#else
	#define debug_fprintf(...) do {} while(0)
#endif

#endif
