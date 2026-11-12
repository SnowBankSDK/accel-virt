#ifndef AV_GUARDED_EXECUTOR_OPS_HPP
#define AV_GUARDED_EXECUTOR_OPS_HPP

#include "device/abi/av_executor.h"
#include "harness/executor/mock_executor_ops.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <pthread.h>

namespace av {

class MockHost;

class GuardedExecutorOps {
public:
    GuardedExecutorOps(MockHost &host, std::uint32_t sub_apis);
    ~GuardedExecutorOps();
    GuardedExecutorOps(const GuardedExecutorOps &) = delete;
    GuardedExecutorOps &operator=(const GuardedExecutorOps &) = delete;

    const AvExecutorOps &table() const;
    MockExecutorOps     &inner();
    const AvExecutorOps &inner_table() const;

    void          arm();
    void          disarm();
    void          block();
    void          unblock();
    bool          await_blocked(std::uint32_t calls, std::uint32_t milliseconds);
    std::uint32_t blocked() const;
    std::uint32_t on_main_loop() const;
    std::uint32_t off_main_loop() const;
    std::size_t   first_on_main_loop() const;

    void enter(std::size_t operation);

private:
    MockHost                  &host_;
    MockExecutorOps            inner_;
    AvExecutorOps              table_;
    pthread_mutex_t            lock_;
    pthread_cond_t             changed_;
    bool                       blocking_;
    std::uint32_t              blocked_;
    std::atomic<bool>          armed_{false};
    std::atomic<std::uint32_t> on_main_loop_{0u};
    std::atomic<std::uint32_t> off_main_loop_{0u};
    std::atomic<std::size_t>   first_{0u};
};

}

#endif
