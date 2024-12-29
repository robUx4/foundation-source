/*****************************************************************************
 *
 * Copyright (c) 2008-2010, CoreCodec, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of CoreCodec, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CoreCodec, Inc. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CoreCodec, Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include "file.h"

bool_t SetFileExt(tchar_t* URL, size_t URLLen, const tchar_t* Ext)
{
    tchar_t *p,*q,*p2;
    bool_t HasHost;

    p = (tchar_t*) GetProtocol(URL,NULL,0,&HasHost);
    q = p;

    p = tcsrchr(q,'\\');
    p2 = tcsrchr(q,'/');
    if (!p || (p2 && p2>p))
        p=p2;
    if (p)
        q = p+1;
    else
    if (HasHost) // only hostname
        return 0;

    if (!q[0]) // no filename at all?
        return 0;

    p = tcsrchr(q,'.');
    if (p)
        *p = 0;

    tcscat_s(URL,URLLen,T("."));
    tcscat_s(URL,URLLen,Ext);
    return 1;
}

void AddPathDelimiter(tchar_t* Path,size_t PathLen)
{
    size_t n = tcslen(Path);
#if defined(TARGET_WIN)
    bool_t HasProtocol = GetProtocol(Path,NULL,0,NULL)==Path;
    if (!n || (n>0 && (HasProtocol || Path[n-1] != '/') && (!HasProtocol || Path[n-1] != '\\')))
    {
        if (HasProtocol)
            tcscat_s(Path,PathLen,T("\\"));
        else
            tcscat_s(Path,PathLen,T("/"));
    }
#else
    if (!n || (n>0 && Path[n-1] != '/'))
        tcscat_s(Path,PathLen,T("/"));
#endif
}

void RemovePathDelimiter(tchar_t* Path)
{
    size_t n = tcslen(Path);
    const tchar_t* s = GetProtocol(Path,NULL,0,NULL);
#if defined(TARGET_WIN)
    bool_t HasProtocol = s==Path;
    if (s[0] && n>0 && ((HasProtocol && Path[n-1] == '\\') || (!HasProtocol && Path[n-1] == '/')))
#else
    if (s[0] && n>0 && Path[n-1] == '/' && n > 1)
#endif
        Path[n-1] = 0;
}

const tchar_t* GetProtocol(const tchar_t* URL, tchar_t* Proto, int ProtoLen, bool_t* HasHost)
{
    const tchar_t* s = tcschr(URL,':');
    if (s && s[1] == '/' && s[2] == '/')
    {
        while (URL<s && IsSpace(*URL)) ++URL;
        if (Proto)
            tcsncpy_s(Proto,ProtoLen,URL,s-URL);
        if (HasHost)
        {
            *HasHost = tcsnicmp(URL,T("file"),4)!=0 &&
                       tcsnicmp(URL,T("conf"),3)!=0 &&
                       tcsnicmp(URL,T("res"),3)!=0 &&
                       tcsnicmp(URL,T("root"),4)!=0 &&
                       tcsnicmp(URL,T("mem"),3)!=0 &&
                       tcsnicmp(URL,T("pose"),4)!=0 &&
                       tcsnicmp(URL,T("vol"),3)!=0 &&
                       tcsnicmp(URL,T("slot"),4)!=0 &&
                       tcsnicmp(URL,T("simu"),4)!=0 &&
                       tcsnicmp(URL,T("local"),5)!=0 &&
                       tcsnicmp(URL,T("sdcard"),6)!=0;
        }
        s += 3;
    }
    else
    {
        if (HasHost)
            *HasHost = 0;
        if (Proto)
            tcscpy_s(Proto,ProtoLen,T("file"));
        s = URL;
    }
    return s;
}

bool_t SplitAddr(const tchar_t* URL, tchar_t* Peer, int PeerLen, tchar_t* Local, int LocalLen)
{
    const tchar_t* p = NULL;
    const tchar_t* p2;
    const tchar_t* Addr;
    bool_t HasHost;
    bool_t Result = 0;

    Addr = GetProtocol(URL,NULL,0,&HasHost);

    if (HasHost)
    {
        p = tcschr(Addr,'\\');
        p2 = tcschr(Addr,'/');
        if (!p || (p2 && p2>p))
            p=p2;
    }
    if (!p)
        p = Addr+tcslen(Addr);

    p2 = tcschr(Addr,'@');
    if (!p2 || p2>p)
        p2 = p;
    else
        Result = 1;

    if (Peer)
        tcsncpy_s(Peer,PeerLen,URL,p2-URL);

    if (Local)
    {
        if (p2<p)
            ++p2;
        tcsncpy_s(Local,LocalLen,URL,Addr-URL);
        tcsncat_s(Local,LocalLen,p2,p-p2);
    }
    return Result;
}

void SplitURL(const tchar_t* URL, tchar_t* Protocol, int ProtocolLen, tchar_t* Host, int HostLen, int* Port, tchar_t* Path, int PathLen)
{
    bool_t HasHost;
    URL = GetProtocol(URL,Protocol,ProtocolLen,&HasHost);

    if (HasHost)
    {
        const tchar_t* p;
        const tchar_t* p2;

        p = tcschr(URL,'\\');
        p2 = tcschr(URL,'/');
        if (!p || (p2 && p2>p))
            p=p2;
        if (!p)
            p = URL+tcslen(URL);

        p2 = tcschr(URL,':');
        if (p2 && p2<p)
        {
            if (Port)
                stscanf(p2+1,T("%d"),Port);
        }
        else
            p2 = p;

        if (Host)
            tcsncpy_s(Host,HostLen,URL,p2-URL);

        URL = p;
    }
    else
    {
        if (Host && HostLen>0)
            *Host = 0;
    }

    if (Path)
    {
        if (URL[0])
        {
            tchar_t* p;
            tcscpy_s(Path,PathLen,URL);
            for (p=Path;*p;++p)
                if (*p == '\\')
                    *p = '/';
        }
        else
            tcscpy_s(Path,PathLen,T("/"));
    }
}

void SplitPath(const tchar_t* URL, tchar_t* Dir, int DirLen, tchar_t* Name, int NameLen, tchar_t* Ext, int ExtLen)
{
    const tchar_t *p,*p2,*p3;
    bool_t HasHost;
    tchar_t LocalURL[MAXPATH];
    tchar_t Protocol[MAXPATH];

    // mime
    p = GetProtocol(URL,Protocol,TSIZEOF(Protocol),&HasHost);

    // dir
    p2 = tcsrchr(p,'\\');
    p3 = tcsrchr(p,'/');
    if (!p2 || (p3 && p3>p2))
        p2 = p3;

    if (p2)
    {
        if (Dir)
            tcsncpy_s(Dir,DirLen,URL,p2-URL);
        URL = p2+1;
    }
    else
    if (HasHost) // no filename, only host
    {
        if (Dir)
            tcscpy_s(Dir,DirLen,URL);
        URL += tcslen(URL);
    }
    else // no directory
    {
        if (Dir)
            tcsncpy_s(Dir,DirLen,URL,p-URL);
        URL = p;
    }

    // name
    if (tcsicmp(Protocol,T("http"))==0 && tcsrchr(URL,T('#')))
    {
        tchar_t *NulChar;
        tcscpy_s(LocalURL,TSIZEOF(LocalURL),URL);
        URL = LocalURL;
        NulChar = tcsrchr(LocalURL,T('#'));
        *NulChar = 0;
    }

    if (Name && Name == Ext)
        tcscpy_s(Name,NameLen,URL);
    else
    {
        p = tcsrchr(URL,'.');
        if (p)
        {
            if (Name)
                tcsncpy_s(Name,NameLen,URL,p-URL);
            if (Ext)
            {
                if (p[1]) ++p; // remove '.', but only if there is a real extension
                tcscpy_s(Ext,ExtLen,p);
            }
        }
        else
        {
            if (Name)
                tcscpy_s(Name,NameLen,URL);
            if (Ext)
                Ext[0] = 0;
        }
    }
}

void RelPath(tchar_t* Rel, int RelLen, const tchar_t* Path, const tchar_t* Base)
{
    size_t n;
    bool_t HasHost;
    const tchar_t* p = GetProtocol(Base,NULL,0,&HasHost);
    if (p != Base)
    {
        if (HasHost)
        {
            // include host name too
            tchar_t *a,*b;
            a = tcschr(p,'\\');
            b = tcschr(p,'/');
            if (!a || (b && b<a))
                a=b;
            if (a)
                p=a;
            else
                p+=tcslen(p);
        }

        // check if mime and host is the same
        n = p-Base;
        if (n>0 && n<tcslen(Path) && (Path[n]=='\\' || Path[n]=='/') && tcsnicmp(Path,Base,n)==0)
        {
            Base += n;
            Path += n;
        }
    }

    n = tcslen(Base);
    if (n>0 && n<tcslen(Path) && (Path[n]=='\\' || Path[n]=='/') && tcsnicmp(Path,Base,n)==0)
        Path += n+1;

    tcscpy_s(Rel,RelLen,Path);
}

bool_t UpperPath(tchar_t* Path, tchar_t* Last, size_t LastLen)
{
    tchar_t *a,*b,*c;
    bool_t HasHost;
    tchar_t Mime[32];

    if (!*Path)
        return 0;

    RemovePathDelimiter(Path);
    c = (tchar_t*)GetProtocol(Path,Mime,TSIZEOF(Mime),&HasHost);

    a = tcsrchr(c,'\\');
    b = tcsrchr(c,'/');
    if (!a || (b && b>a))
        a=b;

    if (!a)
    {
        if (tcsicmp(Mime, T("smb")) == 0) {
            *c = 0;
            tcscpy_s(Last, LastLen, Path);
            return 1;
        }

        if (HasHost && tcsicmp(Mime, T("upnp"))!=0)
            return 0;
        a=c;
        if (!a[0]) // only mime left
            a=c=Path;
    }
    else
        ++a;

    if (Last)
        tcscpy_s(Last,LastLen,a);

    if (a==c)
        *a = 0;

    while (--a>=c && (*a=='\\' || *a=='/'))
        *a = 0;

    return 1;
}

void AbsPath(tchar_t* Abs, int AbsLen, const tchar_t* Path, const tchar_t* Base)
{
    if (Base && GetProtocol(Base,NULL,0,NULL)!=Base && (Path[0] == '/' || Path[0] == '\\') &&
        (Path[1] != '/' && Path[1] != '\\'))
    {
        tchar_t* s;
        bool_t HasHost;

        tcscpy_s(Abs,AbsLen,Base);
        s = (tchar_t*)GetProtocol(Abs,NULL,0,&HasHost);
        if (!HasHost)
        {
            // keep "mime://" from Base
            ++Path;
            *s = 0;
        }
        else
        {
            // keep "mime://host" from Base
            tchar_t *a,*b;
            a = tcschr(s,'\\');
            b = tcschr(s,'/');
            if (!a || (b && b<a))
                a=b;
            if (a)
                *a=0;
        }
    }
    else
    if (Base && GetProtocol(Path,NULL,0,NULL)==Path && Path[0] != '/' && Path[0] != '\\' &&
        !(Path[0] && Path[1]==':' && (Path[2]=='\\' || Path[2]=='\0')))
    {
        // doesn't have mime or drive letter or pathdelimiter at the start
        const tchar_t* MimeEnd = GetProtocol(Base,NULL,0,NULL);
        tcscpy_s(Abs,AbsLen,Base);

#if defined(TARGET_WIN)
        if (MimeEnd==Base)
            AddPathDelimiter(Abs,AbsLen);
        else
#endif
        if (MimeEnd[0])
            AddPathDelimiter(Abs,AbsLen);
    }
    else
        Abs[0] = 0;

    tcscat_s(Abs,AbsLen,Path);
    AbsPathNormalize(Abs,AbsLen);
}

void AbsPathNormalize(tchar_t* Abs,size_t AbsLen)
{
    if (GetProtocol(Abs,NULL,0,NULL)!=Abs)
    {
        tchar_t *i;
        for (i=Abs;*i;++i)
            if (*i == '\\')
                *i = '/';
    }
    else
    {
#if defined(TARGET_WIN)
        tchar_t *i;
        for (i=Abs;*i;++i)
            if (*i == '/')
                *i = '\\';
#endif
    }
}

void ReduceLocalPath(tchar_t* Abs,size_t UNUSED_PARAM(AbsLen))
{
    tchar_t *Folder,*Back;
    Folder = tcsstr(Abs,T("://")); // skip the protocol
    if (Folder)
        Abs = Folder+3;
#if defined(TARGET_WIN)
    Back = tcsstr(Abs,T("\\\\"));
#else
    Back = tcsstr(Abs,T("//"));
#endif
    while (Back)
    {
        memmove(Back,Back+1,tcsbytes(Back+1));
#if defined(TARGET_WIN)
        Back = tcsstr(Abs,T("\\\\"));
#else
        Back = tcsstr(Abs,T("//"));
#endif
    }

#if defined(TARGET_WIN)
    Back = tcsstr(Abs,T("\\.."));
#else
    Back = tcsstr(Abs,T("/.."));
#endif
    while (Back)
    {
        Folder = Back;
        while (--Folder >= Abs)
        {
#if defined(TARGET_WIN)
            if (*Folder == T('\\'))
#else
            if (*Folder == T('/'))
#endif
            {
                memmove(Folder,Back+3,tcsbytes(Back+3));
                break;
            }
        }
#if defined(TARGET_WIN)
        Back = tcsstr(Abs,T("\\.."));
#else
        Back = tcsstr(Abs,T("/.."));
#endif
    }
}

int CheckExts(const tchar_t* URL, const tchar_t* Exts)
{
    tchar_t Ext[MAXPATH];
    tchar_t* Tail;
    intptr_t ExtLen;

    SplitPath(URL,NULL,0,NULL,0,Ext,TSIZEOF(Ext));
    Tail = tcschr(Ext,'?');
    if (Tail) *Tail = 0;
    ExtLen = tcslen(Ext);

    while (Exts)
    {
        const tchar_t* p = tcschr(Exts,':');
        if (p && (ExtLen == p-Exts) && tcsnicmp(Ext,Exts,p-Exts)==0)
            return p[1]; // return type char
        Exts = tcschr(Exts,';');
        if (Exts) ++Exts;
    }
    return 0;
}

int ScaleRound(int_fast32_t v,int_fast32_t Num,int_fast32_t Den)
{
    int64_t i;
    if (!Den)
        return 0;
    i = (int64_t)v * Num;
    if (i<0)
        i-=Den/2;
    else
        i+=Den/2;
    i/=Den;
    return (int)i;
}

extern const nodemeta BufStream_Class[];
extern const nodemeta MemStream_Class[];
extern const nodemeta Streams_Class[];
extern const nodemeta File_Class[];
#if defined(CONFIG_STDIO)
extern const nodemeta Stdio_Class[];
#endif

void CoreC_FileInit(nodemodule* Module)
{
    NodeRegisterClassEx(Module,BufStream_Class);
    NodeRegisterClassEx(Module,MemStream_Class);
    NodeRegisterClassEx(Module,Streams_Class);
    NodeRegisterClassEx(Module,File_Class);
#if defined(CONFIG_STDIO)
    NodeRegisterClassEx(Module,Stdio_Class);
#endif
}
