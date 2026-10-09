// Persistent pool. Contiguous split, one range per thread.
#include "engine/vector/svg/thread_pool.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace pittore::svg {

struct Pool::Impl {
    std::vector<std::thread> workers;
    std::mutex m;
    std::condition_variable cvStart;
    std::condition_variable cvDone;
    Pool::Fn fn;
    std::vector<std::pair<int, int>> ranges;
    int gen = 0;
    int done = 0;
    int active = 0;
    bool exit = false;

    void loop(int lid) {
        int myGen = 0;
        for (;;) {
            std::unique_lock<std::mutex> lk(m);
            cvStart.wait(lk, [&] { return exit || gen != myGen; });
            if (exit) {
                return;
            }
            myGen = gen;
            const int nThreads = active;
            if (lid >= nThreads) {
                continue;
            }
            const auto rg = ranges[(size_t)lid];
            Fn f = fn;
            lk.unlock();
            for (int i = rg.first; i < rg.second; ++i) {
                f(i, lid);
            }
            lk.lock();
            if (++done == active - 1) {
                cvDone.notify_one();
            }
        }
    }
};

Pool::Pool(int size) : impl_(std::make_unique<Impl>()) {
    int total = size <= 0 ? 4 : size;
    if (total < 1) {
        total = 1;
    }
    if (total > 256) {
        total = 256;
    }
    impl_->ranges.resize((size_t)(total > 0 ? total : 1));
    for (int t = 0; t + 1 < total; ++t) {
        impl_->workers.emplace_back([this, t] { impl_->loop(t); });
    }
}

Pool::~Pool() {
    {
        std::lock_guard<std::mutex> lk(impl_->m);
        impl_->exit = true;
    }
    impl_->cvStart.notify_all();
    for (auto& th : impl_->workers) {
        th.join();
    }
}

int Pool::size() const {
    return (int)impl_->workers.size() + 1;
}

void Pool::dispatch(int count, Fn fn) {
    if (count <= 0) {
        return;
    }
    const int total = size();
    int parts = total;
    if (parts > count) {
        parts = count;
    }
    if (parts <= 1) {
        for (int i = 0; i < count; ++i) {
            fn(i, 0);
        }
        return;
    }
    // Contiguous ranges.
    const int base = count / parts;
    const int rest = count % parts;
    int cur = 0;
    {
        std::lock_guard<std::mutex> lk(impl_->m);
        impl_->fn = fn;
        impl_->active = parts;
        impl_->done = 0;
        for (int t = 0; t < parts; ++t) {
            const int n = base + (t < rest ? 1 : 0);
            impl_->ranges[(size_t)t] = {cur, cur + n};
            cur += n;
        }
        ++impl_->gen;
    }
    impl_->cvStart.notify_all();
    // Caller takes last range.
    const auto mine = impl_->ranges[(size_t)(parts - 1)];
    for (int i = mine.first; i < mine.second; ++i) {
        fn(i, parts - 1);
    }
    std::unique_lock<std::mutex> lk(impl_->m);
    impl_->cvDone.wait(lk, [&] { return impl_->done == parts - 1; });
}

namespace {

std::mutex gM;
std::shared_ptr<Pool> gPool;
int gWant = 0;

int resolveWant() {
    if (gWant > 0) {
        return gWant;
    }
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 4 : (int)hw;
}

}  // namespace

void setThreads(int n) {
    std::lock_guard<std::mutex> lk(gM);
    gWant = n < 0 ? 0 : n;
    gPool.reset();
}

std::shared_ptr<Pool> getPool() {
    std::lock_guard<std::mutex> lk(gM);
    if (!gPool) {
        gPool = std::make_shared<Pool>(resolveWant());
    }
    return gPool;
}

}  // namespace pittore::svg
