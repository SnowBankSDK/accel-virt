#include "executor/shim/d3d_headers.hpp"
#include "executor/shim/fence_wait.hpp"
#include "harness/framework/test_registry.hpp"

#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

using av::FenceWait;

namespace {

class ScriptedFence final : public ID3D11Fence {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override
    {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE   AddRef() override { return 1u; }
    ULONG STDMETHODCALLTYPE   Release() override
    {
        released++;
        return 1u;
    }
    void STDMETHODCALLTYPE    GetDevice(ID3D11Device **device) override { *device = nullptr; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT *, void *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(const SECURITY_ATTRIBUTES *, DWORD, LPCWSTR, HANDLE *) override
    {
        return E_NOTIMPL;
    }
    UINT64 STDMETHODCALLTYPE  GetCompletedValue() override { return completed.load(); }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64, HANDLE event) override
    {
        descriptor.store(static_cast<int>(reinterpret_cast<std::intptr_t>(event)));
        return S_OK;
    }

    void reach(std::uint64_t value)
    {
        completed.store(value);
        const std::uint64_t one = 1u;
        static_cast<void>(write(descriptor.load(), &one, sizeof one));
    }

    std::atomic<std::uint64_t> completed{0u};
    std::atomic<int>           descriptor{-1};
    std::atomic<std::uint32_t> released{0u};
};

struct Waiting {
    FenceWait        *waits;
    ScriptedFence    *fence;
    std::atomic<bool> done{false};
    bool              reached = false;
};

void *wait_long(void *argument)
{
    Waiting *const waiting = static_cast<Waiting *>(argument);
    waiting->reached = waiting->waits->wait(waiting->fence, 4u, 60000u);
    waiting->done.store(true);
    return nullptr;
}

}

TEST(fence_wait, a_fence_already_at_its_value_ends_the_wait_without_an_event_and_lets_go_of_the_fence_once)
{ FenceWait     waits;
  ScriptedFence below;
  below.completed = 6u;
  CHECK(!waits.wait(&below, 7u, 0u));
  CHECK_EQ(below.released.load(), 1u);
  ScriptedFence reached;
  reached.completed = 7u;
  CHECK(waits.wait(&reached, 7u, 0u));
  CHECK_EQ(reached.descriptor.load(), -1);
  CHECK_EQ(reached.released.load(), 1u); }

TEST(fence_wait, a_wait_in_one_waiter_does_not_hold_up_a_fence_already_reached_in_another)
{ FenceWait     long_waits;
  FenceWait     short_waits;
  ScriptedFence slow;
  Waiting       waiting;
  waiting.waits = &long_waits;
  waiting.fence = &slow;
  pthread_t thread;
  CHECK_EQ(pthread_create(&thread, nullptr, &wait_long, &waiting), 0);
  while (slow.descriptor.load() < 0) sched_yield();
  ScriptedFence quick;
  quick.completed = 2u;
  CHECK(short_waits.wait(&quick, 2u, 0u));
  CHECK(!waiting.done.load());
  slow.reach(4u);
  pthread_join(thread, nullptr);
  CHECK(waiting.reached);
  CHECK_EQ(slow.released.load(), 1u);
  CHECK_EQ(quick.released.load(), 1u); }
