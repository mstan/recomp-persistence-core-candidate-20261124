#include "worker.h"
#include <future>
#include <new>
#include <chrono>
struct RecompWorker { std::future<void> job; };
extern "C" RecompWorker* recomp_worker_create(void) { return new(std::nothrow) RecompWorker; }
extern "C" void recomp_worker_join(RecompWorker* w) { if(w && w->job.valid()) w->job.get(); }
extern "C" void recomp_worker_start(RecompWorker* w, void (*job)(void*), void* context) {
    if(!w) { job(context); return; }
    recomp_worker_join(w);
    try { w->job=std::async(std::launch::async,[=]{job(context);}); }
    catch (const std::system_error&) { job(context); }
}
extern "C" int recomp_worker_busy(RecompWorker* w) {
    return w && w->job.valid() && w->job.wait_for(std::chrono::seconds(0))!=std::future_status::ready;
}
extern "C" void recomp_worker_destroy(RecompWorker* w) { recomp_worker_join(w);delete w; }
