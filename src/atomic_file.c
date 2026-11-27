// SPDX-License-Identifier: MIT
// Extracted from segagenesisrecomp; see PROVENANCE.md.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "recomp_persistence/atomic_file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
#else
#include <unistd.h>
#endif

struct RecompAtomicFile {
    FILE *stream;
    char *target, *temporary;
    uint64_t limit, written;
    int failed, owns_temporary;
#ifdef _WIN32
    wchar_t *wide_target, *wide_temporary;
#endif
};

#ifdef _WIN32
static wchar_t *atomic_wide_path(const char *path)
{
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(!n)return NULL;
    wchar_t *w=malloc((size_t)n*sizeof *w);
    if(w&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,w,n)){free(w);return NULL;}
    return w;
}

static char *atomic_absolute_path(const char *path)
{
    wchar_t *w=atomic_wide_path(path);
    if(!w)return NULL;
    /* Resolve the caller's path once. Both target and staging names derive
     * from this binding, including if another owner later changes CWD. */
    DWORD capacity=GetFullPathNameW(w,0,NULL,NULL);
    if(!capacity){free(w);return NULL;}
    wchar_t *absolute=malloc((size_t)capacity*sizeof *absolute);
    DWORD count=absolute?GetFullPathNameW(w,capacity,absolute,NULL):0;
    free(w);
    if(!count||count>=capacity){free(absolute);return NULL;}
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,absolute,-1,NULL,0,NULL,NULL);
    char *result=bytes?malloc((size_t)bytes):NULL;
    if(result&&!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,absolute,-1,result,bytes,NULL,NULL)){
        free(result);result=NULL;
    }
    free(absolute);return result;
}

/* FILE_RENAME_INFO's Windows ABI, including SDKs predating its Flags union.
 * Microsoft documents REPLACE_IF_EXISTS (1) + POSIX_SEMANTICS (2) here:
 * https://learn.microsoft.com/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information
 * FileRenameInfoEx is class22; unsupported OS/filesystem classes retain the
 * existing closed-source MoveFileExW path, never truncate/delete the target. */
typedef struct {
    DWORD flags;
    HANDLE root;
    DWORD bytes;
    WCHAR name[1];
} AtomicRenameInfo;
typedef BOOL (WINAPI *AtomicSetFileInfo)(HANDLE,int,LPVOID,DWORD);

static int atomic_replace_windows(RecompAtomicFile *file,HANDLE *publication)
{
    AtomicSetFileInfo set_info=NULL;
#ifndef RECOMP_ATOMIC_TEST_LEGACY_RENAME
    HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
    if(kernel){
        FARPROC address=GetProcAddress(kernel,"SetFileInformationByHandle");
        _Static_assert(sizeof address==sizeof set_info,"Windows procedure pointer ABI");
        memcpy(&set_info,&address,sizeof set_info);
    }
#endif
    DWORD error=ERROR_CALL_NOT_IMPLEMENTED;
    if(set_info){
        size_t bytes=wcslen(file->wide_target)*sizeof(WCHAR);
        size_t prefix=offsetof(AtomicRenameInfo,name);
        if(bytes>UINT32_MAX-prefix-sizeof(WCHAR))return 0;
        size_t size=prefix+bytes+sizeof(WCHAR);
        AtomicRenameInfo *info=calloc(1,size);
        if(!info)return 0;
        info->flags=3;info->bytes=(DWORD)bytes;
        memcpy(info->name,file->wide_target,bytes);
        int ok=set_info(*publication,22,info,(DWORD)size)!=0;
        error=ok?ERROR_SUCCESS:GetLastError();
        free(info);
        if(ok)return 1;
    }
    if(error!=ERROR_INVALID_PARAMETER&&error!=ERROR_INVALID_FUNCTION&&
       error!=ERROR_NOT_SUPPORTED&&error!=ERROR_CALL_NOT_IMPLEMENTED)return 0;
    CloseHandle(*publication);*publication=INVALID_HANDLE_VALUE;
    return MoveFileExW(file->wide_temporary,file->wide_target,
                       MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
#else
static char *atomic_absolute_path(const char *path)
{
    size_t length=strlen(path);
    if(path[0]=='/'){
        char *result=malloc(length+1);
        if(result)memcpy(result,path,length+1);
        return result;
    }
    size_t capacity=256;
    char *working;
    for(;;){
        working=malloc(capacity);
        if(!working)return NULL;
        if(getcwd(working,capacity))break;
        int error=errno;
        free(working);
        if(error!=ERANGE||capacity>SIZE_MAX/2)return NULL;
        capacity*=2;
    }
    size_t prefix=strlen(working);
    if(prefix>SIZE_MAX-length-2){free(working);return NULL;}
    char *result=malloc(prefix+length+2);
    if(result){
        memcpy(result,working,prefix);
        if(prefix&&working[prefix-1]!='/')result[prefix++]='/';
        /* Do not realpath or lexically collapse the caller's symlinks/.. . */
        memcpy(result+prefix,path,length+1);
    }
    free(working);return result;
}
#endif

static FILE *atomic_open_exclusive(RecompAtomicFile *file)
{
#ifdef _WIN32
    /* Deny other writers. Delete sharing allows a following complete commit
     * to replace this version before its short-lived publication handle closes. */
    HANDLE handle=CreateFileW(file->wide_temporary,GENERIC_WRITE|DELETE,
                              FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,NULL);
    if(handle==INVALID_HANDLE_VALUE){
        DWORD error=GetLastError();
        errno=(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)?EEXIST:EIO;
        return NULL;
    }
    file->owns_temporary=1;
    int fd=_open_osfhandle((intptr_t)handle,_O_WRONLY|_O_BINARY);
    if(fd<0){CloseHandle(handle);return NULL;}
    FILE *stream=_fdopen(fd,"wb");
    if(!stream)_close(fd);
    return stream;
#else
    FILE *stream=fopen(file->temporary,"wbx");
    if(stream)file->owns_temporary=1;
    return stream;
#endif
}

void recomp_atomic_file_abort(RecompAtomicFile *file)
{
    if(!file)return;
    if(file->stream)(void)fclose(file->stream);
    if(file->owns_temporary){
#ifdef _WIN32
        (void)_wremove(file->wide_temporary);
#else
        (void)remove(file->temporary);
#endif
    }
#ifdef _WIN32
    free(file->wide_target);free(file->wide_temporary);
#endif
    free(file->target);free(file->temporary);free(file);
}

RecompAtomicFile *recomp_atomic_file_begin(const char *path,uint64_t max_bytes)
{
    if(!path||!*path||strlen(path)>SIZE_MAX-64)return NULL;
    RecompAtomicFile *file=calloc(1,sizeof *file);
    if(!file)return NULL;
    file->target=atomic_absolute_path(path);file->limit=max_bytes;
    if(!file->target||strlen(file->target)>SIZE_MAX-64)goto failed;
    size_t capacity=strlen(file->target)+64;
    file->temporary=malloc(capacity);
    if(!file->temporary)goto failed;
#ifdef _WIN32
    file->wide_target=atomic_wide_path(file->target);
    if(!file->wide_target)goto failed;
    unsigned long pid=GetCurrentProcessId();
#else
    unsigned long pid=(unsigned long)getpid();
#endif
    /* Live allocations are distinct between concurrent transactions. Exclusive
     * creation also preserves stale/foreign files after address/PID reuse. */
    for(unsigned attempt=0;attempt<16;++attempt){
        (void)snprintf(file->temporary,capacity,"%s.tmp-%lu-%p-%u",file->target,pid,(void*)file,attempt);
#ifdef _WIN32
        free(file->wide_temporary);
        file->wide_temporary=atomic_wide_path(file->temporary);
        if(!file->wide_temporary)goto failed;
#endif
        file->stream=atomic_open_exclusive(file);
        if(file->stream)return file;
        if(file->owns_temporary||errno!=EEXIST)break;
    }
failed:
    recomp_atomic_file_abort(file);return NULL;
}

int recomp_atomic_file_write(RecompAtomicFile *file,const void *data,size_t bytes)
{
    if(!file)return 0;
    if(file->failed)return 0;
    if((!data&&bytes)||(uint64_t)bytes>file->limit-file->written){
        file->failed=1;return 0;
    }
    if(bytes&&fwrite(data,1,bytes,file->stream)!=bytes){file->failed=1;return 0;}
    file->written+=(uint64_t)bytes;return 1;
}

int recomp_atomic_file_flush(RecompAtomicFile *file)
{
    if(!file||file->failed)return 0;
    if(fflush(file->stream)||ferror(file->stream)){file->failed=1;return 0;}
    return 1;
}

int recomp_atomic_file_commit(RecompAtomicFile *file)
{
    if(!file)return 0;
    int ok=recomp_atomic_file_flush(file);
#ifdef _WIN32
    HANDLE publication=INVALID_HANDLE_VALUE;
    if(ok&&_commit(_fileno(file->stream)))ok=0;
    if(ok&&!DuplicateHandle(GetCurrentProcess(),(HANDLE)_get_osfhandle(_fileno(file->stream)),
                             GetCurrentProcess(),&publication,0,FALSE,DUPLICATE_SAME_ACCESS))ok=0;
#else
    if(ok&&fsync(fileno(file->stream)))ok=0;
#endif
    if(fclose(file->stream))ok=0;
    file->stream=NULL;
#ifdef _WIN32
    /* The stream must close successfully before publication. Its duplicated
     * DELETE-capable handle retains ownership for the handle-based rename. */
    if(ok)ok=atomic_replace_windows(file,&publication);
    if(publication!=INVALID_HANDLE_VALUE)CloseHandle(publication);
#else
    if(ok&&rename(file->temporary,file->target))ok=0;
#endif
    if(ok)file->owns_temporary=0;
    recomp_atomic_file_abort(file);return ok;
}

int recomp_atomic_file_store(const char *path,const void *data,size_t bytes)
{
    if(!data&&bytes)return 0;
    RecompAtomicFile *file=recomp_atomic_file_begin(path,(uint64_t)bytes);
    if(!file)return 0;
    if(!recomp_atomic_file_write(file,data,bytes)){recomp_atomic_file_abort(file);return 0;}
    return recomp_atomic_file_commit(file);
}
