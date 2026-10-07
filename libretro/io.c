/* Redirect the original engine's configuration and save slots to the frontend save directory. */
#include "core_internal.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
char lr_save_dir[4096];
static const char *map_name(const char *name,char *path,size_t size)
{
    const char *base=name, *p, *extension;
    int written;
    for(p=name;*p;++p)if(*p=='/'||*p=='\\')base=p+1;
    extension=strrchr(base,'.');
    if(!extension || (strcasecmp(extension,".sav") && strcasecmp(extension,".cfg") && strcmp(base,"duke3d.tmp")))return name;
    written=snprintf(path,size,"%s/%s",lr_save_dir,base);
    if(written<0||(size_t)written>=size){errno=ENAMETOOLONG;return NULL;}
    return path;
}
FILE *lr_fopen(const char *name,const char *mode)
{ char path[4096];const char *mapped=map_name(name,path,sizeof(path));return mapped?fopen(mapped,mode):NULL; }
int lr_open(const char *name,int flags,...)
{
    char path[4096];const char *mapped=map_name(name,path,sizeof(path));int mode=0;
    if(flags&O_CREAT){va_list args;va_start(args,flags);mode=va_arg(args,int);va_end(args);}
    return mapped?open(mapped,flags,mode):-1;
}
int lr_unlink(const char *name)
{ char path[4096];const char *mapped=map_name(name,path,sizeof(path));return mapped?unlink(mapped):-1; }
int lr_access(const char *name,int mode)
{ char path[4096];const char *mapped=map_name(name,path,sizeof(path));return mapped?access(mapped,mode):-1; }
