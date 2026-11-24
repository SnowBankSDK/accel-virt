#include "device/control/control_request.hpp"
#include "device/control/held_requests.hpp"
#include "device/control/present_engine_commands.hpp"
#include "device/fence/fence_interrupts.hpp"
#include "device/present/present_engine.hpp"
#include "device/present/present_frames.hpp"
#include "harness/host/mock_host.hpp"
#include "harness/host/mock_request.hpp"
#include "proto/pages/present_progress.hpp"
#include "proto/virtio_gpu/queued_present_request.hpp"
#include "proto/wire/wire_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <time.h>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);

namespace {

constexpr std::uint64_t ram = 0x100000u;
constexpr std::size_t   ram_bytes = 0x10000u;
constexpr std::uint64_t page = ram + 0xE000u;
constexpr std::uint64_t split = ram + 0x7000u;
constexpr std::uint32_t ok_nodata = 0x1100u;

bool in_destination(const std::uint8_t *data, std::size_t size, const av::QueuedPresentRequest &request,
                    std::uint64_t address)
{
    for (std::uint32_t entry = 0u; entry < request.copy().destination_entries(); entry++) {
        const std::size_t at = request.destination_entries_offset() + entry * av::QueuedPresentRequest::entry_size;
        if (at + av::QueuedPresentRequest::entry_size > size) return false;
        std::uint64_t start = 0u;
        std::uint32_t length = 0u;
        std::memcpy(&start, data + at, sizeof start);
        std::memcpy(&length, data + at + 8u, sizeof length);
        if (address >= start && address - start < length) return true;
    }
    return false;
}

bool covers_header(const std::uint8_t *data, std::size_t size, const av::QueuedPresentRequest &request)
{
    for (std::uint64_t address = page; address < page + av::PresentProgress::completed_at; address++)
        if (in_destination(data, size, request, address)) return true;
    return false;
}

bool raised(av::MockHost &host)
{
    const timespec pause = {0, 20000};
    for (std::uint32_t waited = 0u; waited < 50000u; waited++) {
        if (host.raised(av::FenceInterrupts::present_line) != 0u) return true;
        nanosleep(&pause, nullptr);
    }
    return false;
}

std::uint64_t counter(av::MockHost &host, std::size_t at)
{
    std::uint64_t value = 0u;
    std::memcpy(&value, host.guest_ram(page) + at, sizeof value);
    return value;
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    av::MockHost host;
    host.add_guest_ram(ram, ram_bytes);
    host.split_guest_memory_at(split);
    std::uint8_t *const memory = host.guest_ram(ram);
    for (std::size_t at = 0u; at < ram_bytes; at++) memory[at] = static_cast<std::uint8_t>(at * 13u + 5u);

    av::WireReader           reader(data, size);
    av::QueuedPresentRequest present;
    const bool               parsed = present.decode(reader) == av::Status::ok;
    std::uint32_t            response = 0u;
    {
        av::FenceInterrupts interrupts(host);
        av::HeldRequests    held(host);
        av::PresentEngine   engine(host, interrupts, held);
        const av::PresentFrames none;
        if (interrupts.initialize() != av::Status::ok || interrupts.connect() != av::Status::ok) __builtin_trap();
        if (engine.initialize() != av::Status::ok || engine.attach(page, none) != av::Status::ok) __builtin_trap();
        {
            av::MockRequest    mock(data, size);
            av::ControlRequest request(host, &mock);
            av::PresentEngineCommands commands(host, engine, none, held);
            if (!commands.answer(request, av::QueuedPresentRequest::type) || !mock.answered()) __builtin_trap();
            response = mock.response_type();
        }
        if (!raised(host)) __builtin_trap();
        const std::uint64_t completed = counter(host, av::PresentProgress::completed_at);
        const std::uint64_t refused = counter(host, av::PresentProgress::refused_at);
        if (completed != 1u || refused > 1u) __builtin_trap();
        if (response != ok_nodata && refused != 1u) __builtin_trap();
        av::PresentProgress progress;
        const bool painted = response == ok_nodata && parsed && covers_header(data, size, present);
        if (!painted && progress.decode(host.guest_ram(page), av::PresentProgress::encoded_size) != av::Status::ok)
            __builtin_trap();
        engine.detach();
    }
    if (host.live_guest_mappings() != 0u || host.live_threads() != 0u) __builtin_trap();

    const bool taken = response == ok_nodata && parsed;
    for (std::size_t at = 0u; at < ram_bytes; at++) {
        if (ram + at >= page && ram + at < page + av::PresentProgress::page_size) continue;
        if (memory[at] == static_cast<std::uint8_t>(at * 13u + 5u)) continue;
        if (!taken || !in_destination(data, size, present, ram + at)) __builtin_trap();
    }
    return 0;
}
