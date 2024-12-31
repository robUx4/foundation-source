/*
  Copyright (c) 2006-2010, CoreCodec, Inc.
  SPDX-License-Identifier: BSD-3-Clause
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_REDIRECT    1024

#ifndef MAX_PATH
#define MAX_PATH		1024
#endif

#if !defined(_MSC_VER) && !defined(__MINGW32__)
# define make_dir(x) mkdir(x,S_IRWXU)
#else
# define make_dir(x) mkdir(x)
#endif

#ifdef _WIN32
#include <io.h>
#include <direct.h>

#ifndef _INTPTR_T_DEFINED
typedef signed int intptr_t;
#define _INTPTR_T_DEFINED
#endif

struct dirent
{
    char d_name[MAX_PATH];
};
typedef struct DIR
{
    intptr_t h;
    int first;
    struct _finddata_t file;
    struct dirent entry;

} DIR;

static DIR* opendir(const char* name)
{
    DIR* p = malloc(sizeof(DIR));
    if (p)
    {
        sprintf(p->entry.d_name,"%s\\*",name);
        p->h = _findfirst(p->entry.d_name,&p->file);
        p->first = 1;
        if (p->h == -1)
        {
            free(p);
            p = NULL;
        }
    }
    return p;
}

static struct dirent* readdir(DIR* p)
{
    if (p->first || _findnext(p->h,&p->file)==0)
    {
        p->first = 0;
        strcpy(p->entry.d_name,p->file.name);
        return &p->entry;
    }
    return NULL;
}

static void closedir(DIR* p)
{
    _findclose(p->h);
    free(p);
}

#else
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#endif

static void trim(char* s)
{
    while (s[0] && isspace(s[strlen(s)-1]))
        s[strlen(s)-1]=0;
}

static void addendpath(char* path)
{
    if (path[0] && path[strlen(path)-1]!='/')
        strcat(path,"/");
}

static void create_missing_dirs(const char *path)
{
    size_t strpos = 0, strpos_i;
    char new_path[MAX_PATH];
    char orig_path[MAX_PATH];
    int ret;
    struct stat dir_stats;

    getcwd(orig_path, sizeof(new_path));

    for (;;)
    {
        strpos_i = strcspn(&path[strpos], "/\\");
        if (strpos_i == strlen(&path[strpos]))
            break;
        strpos += strpos_i+1;

        strcpy(new_path,orig_path);
        strcat(new_path,"/");
        strncat(new_path, path, strpos-1);

        if (stat(new_path, &dir_stats) == 0)
        {
            if ((dir_stats.st_mode & S_IFDIR) != S_IFDIR)
            {
                printf("%s is not a directory\n",new_path);
                exit(1);
            }
        }
        else
        {
            ret = make_dir(new_path);

            if (ret != 0 && ret != EEXIST)
            {
                printf("can't create directory %s\n",new_path);
                exit(1);
            }
        }
    }
}

static size_t redirect_count;
static char redirect_src[MAX_REDIRECT][MAX_PATH];
static char redirect_dst[MAX_REDIRECT][MAX_PATH];

static int compare(const char* srcname, const char* dstname)
{
    char srcbuf[4096];
    char dstbuf[4096];
    const char* tmpname = NULL;
    FILE* src;
    FILE* dst = NULL;
    int ret=0;

    src = fopen(srcname,"rb");
    if (!src)
    {
        if (tmpname)
            remove(tmpname);
        printf("file open error (%s)!\n",srcname);
        exit(1);
    }

    dst = fopen(dstname,"rb");
    if (dst)
    {
        for (;;)
        {
            size_t n = fread(srcbuf,1,sizeof(srcbuf),src);
            size_t m = fread(dstbuf,1,sizeof(dstbuf),dst);
            if (n != m || memcmp(srcbuf,dstbuf,n)!=0)
            {
                ret=1;
                break;
            }
            if (!n)
                break;
        }
        fclose(dst);
    }
    else
        ret = 1;

    fclose(src);
    if (tmpname)
        remove(tmpname);

    return ret;
}

static void copy(const char* srcname, const char* dstname)
{
    char buf[4096];
    FILE* src;
    FILE* dst;
    const char* tmpname = NULL;
    struct stat s;

    printf("%s -> %s\n",srcname,dstname);

    stat(srcname,&s);

    src = fopen(srcname,"rb");
    if (!src)
    {
        printf("file open error (%s)!\n",srcname);
        exit(1);
    }

    create_missing_dirs(dstname);

    dst = fopen(dstname,"wb+");
    if (!dst)
    {
        printf("file create error (%s)!\n",dstname);
        exit(1);
    }

    for (;;)
    {
        size_t n = fread(buf,1,sizeof(buf),src);
        if (!n) break;
        fwrite(buf,1,n,dst);
    }

    fclose(src);
    fclose(dst);

    chmod(dstname,s.st_mode);

    if (tmpname)
        remove(tmpname);
}

static int same_ch(int a, int b)
{
#ifdef _WIN32
    return toupper(a) == toupper(b);
#else
    return a==b;
#endif
}

static int match(const char* name, const char* mask)
{
    if (*mask=='*')
    {
        ++mask;
        for (;!match(name,mask);++name)
        {
            if (*name==0)
                return 0;
        }
        return 1;
    }
    else
    {
        if (*mask==0)
            return *name==0;

        if (*name==0)
            return 0;

        return (mask[0]=='?' || same_ch(name[0],mask[0])) && match(name+1,mask+1);
    }
}

static void refresh(const char* src, const char* dst, int force)
{
    DIR* dir;
    size_t i;

    if (strcmp(dst,"-")==0 || strchr(src,'*') || strchr(src,'?'))
        return;

    if (!force)
        for (i=0;i<redirect_count;++i)
            if (match(src,redirect_src[i]))
                return;

    dir = opendir(src);
    if (dir)
    {
        struct dirent* entry;
        make_dir(dst);
        if (!force || dst[strlen(dst)-1]!='.')
        {
            while ((entry = readdir(dir)) != NULL)
            {
                if (entry->d_name[0]!='.')
                {
                    char src2[MAX_PATH];
                    char dst2[MAX_PATH];

                    strcpy(dst2,dst);
                    strcpy(src2,src);
                    addendpath(dst2);
                    addendpath(src2);
                    strcat(src2,entry->d_name);
                    strcat(dst2,entry->d_name);
                    refresh(src2,dst2,0);
                }
            }
        }
        closedir(dir);
    }
    else
    {
        if (compare(src,dst))
            copy(src,dst);
    }
}

int main(int argc, char** argv)
{
    FILE* f;
    char base[MAX_PATH];
    char line[MAX_PATH*2];
    size_t i;

    if (argc<=1)
    {
        printf("usage: branch <config>\n");
        return 1;
    }

    strcpy(base,argv[1]);
    for (i=strlen(base);i>0;)
    {
        --i;
        if (base[i]=='/' || base[i]=='\\')
            break;
        base[i]=0;
    }

    f = fopen(argv[1],"r");
    if (!f)
    {
        printf("%s not found!\n",argv[1]);
        return 1;
    }

    redirect_count=0;
    for (;;)
    {

        char* s, *t;
        if (!fgets(line,sizeof(line),f))
            break;

        if (line[0]=='#')
            continue;

        {
            trim(line);
            strcpy(redirect_src[redirect_count],(line[0]!='/')?base:"");
            strcat(redirect_src[redirect_count],line);

            s = redirect_src[redirect_count];
            s += (line[0]!='/')?strlen(base):0;
            while (*s && isspace(*s))
                ++s;
            if (*s == '\"')
            {
                memmove(s,s+1,strlen(s)+1);
                t = strchr(s+1,'\"');
                if (t)
                    *t=0;
                s = t+1;
            }
            else
            {
                s = strchr(redirect_src[redirect_count],' ');
            }

            if (s)
            {
                while (*s && isspace(*s))
                    *(s++)=0;
                if (*s == '\"')
                {
                    t = strchr(++s,'\"');
                    if (t)
                        *t=' ';
                    strcpy(redirect_dst[redirect_count],s);
                    s = strchr(t+1,' ');
                }
                else
                {
                    strcpy(redirect_dst[redirect_count],s);
                    s = strchr(redirect_dst[redirect_count],' ');
                }

                ++redirect_count;
            }
        }
    }

    fclose(f);

    for (i=0;i<redirect_count;++i)
        refresh(redirect_src[i],redirect_dst[i],1);

    return 0;
}
