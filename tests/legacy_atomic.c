// SPDX-License-Identifier: MIT
/* Execute the existing MoveFileEx fallback even on a host supporting the newer
 * handle class. The production interface and transaction owner are identical. */
#include "recomp_persistence/atomic_file.h"
#include <stdio.h>
#include <string.h>
static int content(const char *expected) {
    char bytes[32]={0};FILE *file=fopen("legacy.sav","rb");
    if(!file)return 0;
    size_t size=fread(bytes,1,sizeof bytes,file);fclose(file);
    return size==strlen(expected)&&!memcmp(bytes,expected,size);
}
int main(void) {
    if(!recomp_atomic_file_store("legacy.sav","old",3)||!content("old"))return 1;
    RecompAtomicFile *file=recomp_atomic_file_begin("legacy.sav",3);
    if(!file||!recomp_atomic_file_write(file,"prefix",0))return 2;
    if(recomp_atomic_file_write(file,"partial",7)||recomp_atomic_file_commit(file)||!content("old"))return 3;
    file=recomp_atomic_file_begin("legacy.sav",3);
    if(!file||!recomp_atomic_file_write(file,"new",3))return 4;
    recomp_atomic_file_abort(file);
    if(!content("old"))return 5;
    if(!recomp_atomic_file_store("legacy.sav","complete",8)||!content("complete"))return 6;
    if(recomp_atomic_file_begin("missing/legacy.sav",0))return 7;
    remove("legacy.sav");
    return 0;
}
