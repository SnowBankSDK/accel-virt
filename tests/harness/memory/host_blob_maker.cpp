#include "harness/memory/host_blob_maker.hpp"

#include "device/control/control_request.hpp"
#include "device/memory/host_blob_table.hpp"
#include "harness/host/mock_host.hpp"
#include "harness/host/mock_request.hpp"

#include <time.h>

namespace av {

namespace {

std::uint32_t status_answer(Status status) { return static_cast<std::uint32_t>(status); }

std::uint64_t now_ms()
{
    timespec time = {};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return static_cast<std::uint64_t>(time.tv_sec) * 1000u + static_cast<std::uint64_t>(time.tv_nsec) / 1000000u;
}

const std::uint8_t empty_request[24] = {};

}

HostBlobMaker::HostBlobMaker(MockHost &host, HostBlobTable &table)
    : host_(host), table_(table), recorder_(host), held_(host), service_(host)
{
}

HostBlobMaker::~HostBlobMaker()
{
    static_cast<void>(settle());
}

Status HostBlobMaker::initialize(ContextPlane &plane)
{
    const Status started = service_.start();
    if (started != Status::ok) return started;
    return table_.initialize(plane, service_, held_, recorder_);
}

bool HostBlobMaker::settle()
{
    const std::uint64_t deadline = now_ms() + patience_ms;
    for (;;) {
        static_cast<void>(host_.run_main_loop());
        if (service_.outstanding() == 0u && table_.pending() == 0u) return true;
        if (now_ms() >= deadline) return false;
        static_cast<void>(host_.wait_for_main_loop_work(1u, 10u));
    }
}

Status HostBlobMaker::answered(MockRequest &request)
{
    if (!settle() || !request.answered()) return Status::invalid_state;
    return static_cast<Status>(request.response_type());
}

Status HostBlobMaker::create(std::uint32_t id, std::uint32_t context, HostMemoryRequest request, std::uint64_t size)
{
    MockRequest    mock(empty_request, sizeof empty_request);
    ControlRequest control(host_, &mock);
    std::uint32_t  ticket = HeldRequests::no_ticket;
    const Status   held = held_.hold(control, &status_answer, ticket);
    if (held != Status::ok) return held;
    const Status handed = table_.create(id, context, request, size, ticket);
    if (handed != Status::ok) held_.conclude(ticket, handed);
    return answered(mock);
}

Status HostBlobMaker::adopt(std::uint32_t id, const MockRequest &prototype, std::size_t offset,
                            std::uint32_t entries)
{
    MockRequest    request(prototype.bytes(), prototype.size());
    ControlRequest control(host_, &request);
    if (!table_.waits_for_backing(id)) return table_.adopt(id, control, offset, entries);
    std::uint32_t ticket = HeldRequests::no_ticket;
    const Status  held = held_.hold(control, &status_answer, ticket);
    if (held != Status::ok) return held;
    const Status handed = table_.begin_adopting(id, control, offset, entries, ticket);
    if (handed != Status::ok) held_.conclude(ticket, handed);
    return answered(request);
}

Status HostBlobMaker::release(std::uint32_t id)
{
    const Status released = table_.release(id);
    static_cast<void>(settle());
    return released;
}

void HostBlobMaker::release_all()
{
    table_.release_all();
    static_cast<void>(settle());
}

HeldRequests  &HostBlobMaker::held() { return held_; }
ServiceThread &HostBlobMaker::service() { return service_; }
WireRecorder  &HostBlobMaker::recorder() { return recorder_; }

}
