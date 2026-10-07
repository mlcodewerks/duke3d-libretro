#ifndef DUKE_RETRO_COMPAT_H
#define DUKE_RETRO_COMPAT_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#ifdef _WIN32
#include <direct.h>
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER 1234
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error The libretro software renderer currently requires a little-endian target.
#endif
#define kmalloc malloc
#define kkmalloc malloc
#define kfree free
#define kkfree free
#define FP_OFF(x) ((intptr_t)(x))
#ifndef __int64
#define __int64 int64_t
#endif
#define stricmp strcasecmp
#define strcmpi strcasecmp
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef S_IREAD
#define S_IREAD S_IRUSR
#endif
#ifndef S_IWRITE
#define S_IWRITE S_IWUSR
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
FILE *lr_fopen(const char *name, const char *mode);
int lr_open(const char *name, int flags, ...);
int lr_unlink(const char *name);
int lr_access(const char *name, int mode);
#define access lr_access
#define fopen lr_fopen
#define open lr_open
#define unlink lr_unlink
void lr_exit(int status);
#define exit lr_exit
#endif
