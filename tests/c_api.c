#include "recomp_persistence/native_writer.h"
#include "recomp_persistence/atomic_file.h"
int main(void) {
    if(recomp_persist_create(0,0,0,0)) return 1;
    if(recomp_atomic_file_store(0,0,0)) return 2;
    if(recomp_persist_submit(0,0)!=-1) return 3;
    if(!recomp_persist_destroy(0)) return 4;
    return 0;
}
