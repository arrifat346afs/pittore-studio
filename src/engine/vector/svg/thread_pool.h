#pragma once
// Counter pool for pixel loops. Inline when small.
#include <functional>
#include <memory>

namespace pittore::svg {

class Pool {
  public:
    using Fn = std::function<void(int, int)>;
    explicit Pool(int size);
    ~Pool();
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    // Total threads including caller.
    int size() const;
    // Run fn(i, localId) for i in [0, count).
    void dispatch(int count, Fn fn);
    // Run inline when bigEnough is false.
    template <typename F>
    void dispatch_threshold(int count, bool bigEnough, F&& fn) {
        if (!bigEnough || count <= 0) {
            for (int i = 0; i < count; ++i) {
                fn(i, 0);
            }
            return;
        }
        dispatch(count, Fn(fn));
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Global pool. 0 means auto.
void setThreads(int n);
std::shared_ptr<Pool> getPool();

}  // namespace pittore::svg
