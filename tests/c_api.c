#include "recomp_persistence/native_writer.h"
#include "recomp_persistence/atomic_file.h"
int main(void) {
    if(recomp_persist_create(0,0,0,0)) return 1;
    if(recomp_atomic_file_store(0,0,0)) return 2;
    if(recomp_persist_submit(0,0)!=-1) return 3;
    if(!recomp_persist_destroy(0)) return 4;
    if(recomp_atomic_file_begin(0,0)) return 5;
    if(recomp_atomic_file_write(0,0,0)) return 6;
    if(recomp_atomic_file_flush(0)) return 7;
    if(recomp_atomic_file_commit(0)) return 8;
    recomp_atomic_file_abort(0);
    return 0;
}
