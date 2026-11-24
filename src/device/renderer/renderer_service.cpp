#include "device/renderer/renderer_service.hpp"

#include "core/trace/function_scope.hpp"
#include "device/control/control_request.hpp"
#include "device/host/host.hpp"
#include "device/memory/host_memory_free.hpp"
#include "device/renderer/doorbell_offers.hpp"
#include "device/renderer/forwarded_request.hpp"
#include "device/renderer/guest_memory_table.hpp"
#include "device/renderer/renderer_resources.hpp"
#include "proto/context/context_setup.hpp"
#include "proto/renderer/bind_context_message.hpp"
#include "proto/renderer/blob_retired_message.hpp"
#include "proto/renderer/context_message.hpp"
#include "proto/renderer/export_image_message.hpp"
#include "proto/renderer/hello_message.hpp"
#include "proto/renderer/allocate_memory_message.hpp"
#include "proto/renderer/allocated_memory_message.hpp"
#include "proto/renderer/free_memory_message.hpp"
#include "proto/renderer/interrupt_lines_message.hpp"
#include "proto/renderer/ready_message.hpp"
#include "proto/renderer/region_message.hpp"
#include "proto/renderer/reply_message.hpp"
#include "proto/renderer/shared_released_message.hpp"
#include "proto/wire/wire_writer.hpp"

namespace av {

namespace {
constexpr size_t   entry_size = 16u;
constexpr uint64_t largest_entry = 0xFFFFFFFFu;

}

RendererService::RendererService(Host &host, RendererResources &resources, GuestMemoryTable &memory,
                                 DoorbellOffers &doorbells)
    : host_(host), resources_(resources), memory_(memory), doorbells_(doorbells), recorder_(host), blobs_(host),
      owners_(host), interrupts_(host), plane_(host, blobs_, owners_, interrupts_, recorder_), capset_(),
      ready_(false), sent_{}, extents_{}, entries_{}, late_{}, export_thread_(host), memory_thread_(host),
      exports_(host), allocations_(host)
{
    AV_SCOPE("RendererService::RendererService");
    for (AvExportedImage &sent : sent_) sent.fd = -1;
}

RendererService::~RendererService()
{
    AV_SCOPE("RendererService::~RendererService");
    shut_down();
}

Status RendererService::receive(const uint8_t *datagram, size_t length, const int *descriptors, uint32_t count,
                                uint8_t *answer, size_t capacity, size_t &answer_length, int &answer_descriptor)
{
    AV_SCOPE("RendererService::receive");
    host_.require_main_loop("RendererService::receive");
    answer_length = 0u;
    answer_descriptor = -1;
    RendererMessageHeader header;
    if (datagram == nullptr || header.decode(datagram, length) != Status::ok) {
        close_all(descriptors, count);
        return Status::malformed;
    }
    const uint8_t *const body = datagram + RendererMessageHeader::encoded_size;
    const uint32_t       body_length = header.body_length();
    using Kind = RendererMessageHeader::Kind;
    const bool takes_descriptors = header.kind() == Kind::region || header.kind() == Kind::bind_context ||
                                   header.kind() == Kind::interrupt_lines;
    if (!takes_descriptors) close_all(descriptors, count);
    if (header.kind() == Kind::hello) return hello(header, body, count, answer, capacity, answer_length);
    if (!ready_ && header.kind() != Kind::interrupts_off) {
        if (takes_descriptors) close_all(descriptors, count);
        return Status::invalid_state;
    }
    Status outcome = Status::ok;
    Status decoded = Status::malformed;
    switch (header.kind()) {
    case Kind::region: return region(body, body_length, descriptors, count);
    case Kind::guest_blob: return guest_blob(body, body_length, count);
    case Kind::allocate_memory: return allocate_memory(header, body, count);
    case Kind::free_memory: return free_memory(body, body_length, count);
    case Kind::blob_retired: return blob_retired(body, body_length, count);
    case Kind::shared_released: return shared_released(body, body_length, count);
    case Kind::interrupt_lines: return interrupt_lines(body, body_length, descriptors, count);
    case Kind::interrupts_off:
        interrupts_.disconnect();
        resources_.drop_interrupt_lines();
        return Status::ok;
    case Kind::create_context:
    case Kind::destroy_context: decoded = context(header.kind(), body, body_length, count, outcome); break;
    case Kind::bind_context: decoded = bind(body, body_length, descriptors, count, outcome); break;
    case Kind::export_image: return export_image(header, body, count);
    default: return Status::malformed;
    }
    if (decoded != Status::ok) return decoded;
    return answer_with(header.sequence(), outcome, answer, capacity, answer_length);
}

Status RendererService::hello(const RendererMessageHeader &header, const uint8_t *body, uint32_t count,
                              uint8_t *answer, size_t capacity, size_t &answer_length)
{
    AV_SCOPE("RendererService::hello");
    HelloMessage hello;
    if (ready_ || hello.decode(body, header.body_length(), count) != Status::ok ||
        hello.version() != HelloMessage::protocol_version || capset_.decode(hello.capset(), hello.capset_length()) != Status::ok)
        return Status::malformed;
    const AvExecutorOps *ops = nullptr;
    const Status loaded = resources_.load_executor(hello.executor_path(), hello.executor_path_length(), ops);
    if (loaded != Status::ok) return loaded;
    if (plane_.accept(ops) != Status::ok || ops == nullptr) return Status::not_supported;
    if (blobs_.initialize() == Status::no_memory || owners_.initialize() == Status::no_memory ||
        interrupts_.initialize() == Status::no_memory || plane_.initialize() == Status::no_memory ||
        export_thread_.start() == Status::no_memory || memory_thread_.start() == Status::no_memory)
        return Status::no_memory;
    static_cast<void>(plane_.start(hello.gpu_uuid(), capset_));
    const uint32_t     sub_apis = plane_.sub_apis() & ((1u << ReadyMessage::lanes) - 1u);
    const ReadyMessage ready(HelloMessage::protocol_version, ops->abi_version, sub_apis);
    const RendererMessageHeader out(RendererMessageHeader::Kind::ready, ReadyMessage::encoded_size,
                                    header.sequence());
    if (capacity < RendererMessageHeader::encoded_size + ReadyMessage::encoded_size) return Status::too_large;
    static_cast<void>(out.encode(answer, capacity));
    static_cast<void>(ready.encode(answer + RendererMessageHeader::encoded_size,
                                   capacity - RendererMessageHeader::encoded_size));
    answer_length = RendererMessageHeader::encoded_size + ReadyMessage::encoded_size;
    ready_ = true;
    return Status::ok;
}

Status RendererService::region(const uint8_t *body, uint32_t length, const int *descriptors, uint32_t count)
{
    AV_SCOPE("RendererService::region");
    RegionMessage message;
    if (message.decode(body, length, count) != Status::ok) {
        close_all(descriptors, count);
        return Status::malformed;
    }
    void        *base = nullptr;
    const Status mapped = resources_.map_region(descriptors[0], message.size(), base);
    if (mapped != Status::ok) {
        resources_.close_descriptor(descriptors[0]);
        return mapped;
    }
    const Status added = memory_.add(message.region(), base, message.size(), descriptors[0]);
    if (added == Status::ok) return Status::ok;
    resources_.unmap_region(base, message.size());
    resources_.close_descriptor(descriptors[0]);
    return added;
}

Status RendererService::guest_blob(const uint8_t *body, uint32_t length, uint32_t count)
{
    AV_SCOPE("RendererService::guest_blob");
    GuestBlobMessage message;
    if (message.decode(body, length, count, extents_, GuestBlobMessage::max_extents) != Status::ok)
        return Status::malformed;
    WireWriter writer(entries_, sizeof entries_);
    for (uint32_t index = 0u; index < message.count(); index++) {
        const RendererExtent &extent = extents_[index];
        if (extent.length() > largest_entry) return Status::invalid_parameter;
        writer.write_u64(static_cast<uint64_t>(extent.region()) << RendererExtent::region_shift | extent.offset());
        writer.write_u32(static_cast<uint32_t>(extent.length()));
        writer.write_u32(0u);
    }
    ForwardedRequest forwarded(entries_, message.count() * entry_size);
    ControlRequest   request(host_, &forwarded);
    return blobs_.create_guest(message.blob(), message.size(), request, 0u, message.count());
}

Status RendererService::allocate_memory(const RendererMessageHeader &header, const uint8_t *body, uint32_t count)
{
    AV_SCOPE("RendererService::allocate_memory");
    AllocateMemoryMessage request;
    if (request.decode(body, header.body_length(), count) != Status::ok) return Status::malformed;
    MemoryAllocation *errand = nullptr;
    if (allocations_.create(errand, *this, plane_, header.sequence(), request) != Status::ok) {
        static_cast<void>(AllocatedMemoryMessage().encode(late_ + RendererMessageHeader::encoded_size,
                                                          AllocatedMemoryMessage::encoded_size));
        send_late(RendererMessageHeader::Kind::allocated_memory, header.sequence(),
                  AllocatedMemoryMessage::encoded_size, -1);
        return Status::ok;
    }
    if (memory_thread_.post(*errand) != Status::ok) errand->conclude();
    return Status::ok;
}

void RendererService::allocated(MemoryAllocation &errand, bool ran)
{
    AV_SCOPE("RendererService::allocated");
    host_.require_main_loop("RendererService::allocated");
    const AllocateMemoryMessage &request = errand.request();
    const AvHostMemory          &memory = errand.memory();
    AllocatedMemoryMessage       allocated;
    int                          descriptor = -1;
    if (ran && errand.status() == Status::ok) {
        const AllocatedMemoryMessage described(memory.caching, memory.size);
        const bool kept = !errand.forsaken() && memory.fd >= 0 && request.key() <= UINT32_MAX &&
                          described.encode(late_ + RendererMessageHeader::encoded_size,
                                           AllocatedMemoryMessage::encoded_size) == Status::ok &&
                          owners_.adopt(static_cast<uint32_t>(request.key()), request.context()) == Status::ok;
        if (kept) {
            allocated = described;
            descriptor = memory.fd;
        } else {
            if (memory.fd >= 0) resources_.close_descriptor(memory.fd);
            if (errand.frees_itself()) plane_.free_memory(request.key());
        }
    }
    static_cast<void>(allocated.encode(late_ + RendererMessageHeader::encoded_size,
                                       AllocatedMemoryMessage::encoded_size));
    send_late(RendererMessageHeader::Kind::allocated_memory, errand.sequence(), AllocatedMemoryMessage::encoded_size,
              descriptor);
    if (descriptor >= 0) resources_.close_descriptor(descriptor);
    static_cast<void>(allocations_.destroy(&errand));
}

Status RendererService::free_memory(const uint8_t *body, uint32_t length, uint32_t count)
{
    AV_SCOPE("RendererService::free_memory");
    FreeMemoryMessage message;
    if (message.decode(body, length, count) != Status::ok) return Status::malformed;
    if (message.key() <= UINT32_MAX) owners_.forget(static_cast<uint32_t>(message.key()));
    const auto of_key = [&message](const MemoryAllocation &allocation) {
        return allocation.request().key() == message.key();
    };
    const auto asked = [&of_key](const MemoryAllocation &allocation) {
        return of_key(allocation) && !allocation.forsaken();
    };
    const Status handed = HostMemoryFree::hand_off(host_, plane_, memory_thread_, message.key());
    for (MemoryAllocation *allocation = allocations_.find_if(asked); allocation != nullptr;
         allocation = allocations_.find_if(asked)) {
        if (handed == Status::ok) allocation->forsake();
        else allocation->disown();
    }
    if (handed != Status::ok && allocations_.find_if(of_key) == nullptr) plane_.free_memory(message.key());
    return Status::ok;
}

Status RendererService::shared_released(const uint8_t *body, uint32_t length, uint32_t count)
{
    AV_SCOPE("RendererService::shared_released");
    SharedReleasedMessage message;
    if (message.decode(body, length, count) != Status::ok) return Status::malformed;
    plane_.shared_released(capset_.gpu().uuid(), message.object_id());
    return Status::ok;
}

Status RendererService::blob_retired(const uint8_t *body, uint32_t length, uint32_t count)
{
    AV_SCOPE("RendererService::blob_retired");
    BlobRetiredMessage message;
    if (message.decode(body, length, count) != Status::ok) return Status::malformed;
    return blobs_.retire(message.blob());
}

Status RendererService::context(RendererMessageHeader::Kind kind, const uint8_t *body, uint32_t length,
                                uint32_t count, Status &outcome)
{
    AV_SCOPE("RendererService::context");
    ContextMessage message;
    if (message.decode(body, length, count) != Status::ok) return Status::malformed;
    outcome = kind == RendererMessageHeader::Kind::create_context ? plane_.create(message.context(), message.name())
                                                                  : plane_.destroy(message.context());
    return Status::ok;
}

Status RendererService::bind(const uint8_t *body, uint32_t length, const int *descriptors, uint32_t count,
                             Status &outcome)
{
    AV_SCOPE("RendererService::bind");
    BindContextMessage message;
    ContextSetup       setup;
    if (message.decode(body, length, count) != Status::ok ||
        setup.decode(message.setup(), message.setup_length()) != Status::ok) {
        close_all(descriptors, count);
        return Status::malformed;
    }
    if (message.has_doorbell() && doorbells_.offer(message.doorbell(), descriptors[0]) != Status::ok) {
        resources_.close_descriptor(descriptors[0]);
        outcome = Status::conflict;
        return Status::ok;
    }
    outcome = plane_.bind(message.context(), setup, capset_);
    int left = -1;
    if (message.has_doorbell() && doorbells_.take(message.doorbell(), left) == Status::ok)
        resources_.close_descriptor(left);
    return Status::ok;
}

Status RendererService::interrupt_lines(const uint8_t *body, uint32_t length, const int *descriptors,
                                        uint32_t count)
{
    AV_SCOPE("RendererService::interrupt_lines");
    InterruptLinesMessage message;
    if (message.decode(body, length, count) != Status::ok) {
        close_all(descriptors, count);
        return Status::malformed;
    }
    int      lines[InterruptLinesMessage::max_lines] = {};
    uint32_t taken = 0u;
    for (uint32_t line = 0u; line < message.lines(); line++) lines[line] = message.present(line) ? descriptors[taken++] : -1;
    resources_.take_interrupt_lines(lines, message.lines());
    return interrupts_.connect();
}

Status RendererService::answer_with(uint64_t sequence, Status outcome, uint8_t *answer, size_t capacity,
                                    size_t &answer_length) const
{
    AV_SCOPE("RendererService::answer_with");
    const ReplyMessage          reply(sequence, outcome, nullptr, 0u);
    const RendererMessageHeader header(RendererMessageHeader::Kind::reply, static_cast<uint32_t>(reply.encoded_length()),
                                       sequence);
    if (answer == nullptr || capacity < RendererMessageHeader::encoded_size + reply.encoded_length())
        return Status::too_large;
    static_cast<void>(header.encode(answer, capacity));
    static_cast<void>(reply.encode(answer + RendererMessageHeader::encoded_size,
                                   capacity - RendererMessageHeader::encoded_size));
    answer_length = RendererMessageHeader::encoded_size + reply.encoded_length();
    return Status::ok;
}

Status RendererService::export_image(const RendererMessageHeader &header, const uint8_t *body, uint32_t count)
{
    AV_SCOPE("RendererService::export_image");
    ExportImageMessage request;
    if (request.decode(body, header.body_length(), count) != Status::ok) return Status::malformed;
    ImageExport *errand = nullptr;
    if (exports_.create(errand, *this, plane_, capset_.gpu().uuid(), header.sequence(), request.key()) != Status::ok) {
        static_cast<void>(ExportedImageMessage().encode(late_ + RendererMessageHeader::encoded_size,
                                                        ExportedImageMessage::encoded_size));
        send_late(RendererMessageHeader::Kind::exported_image, header.sequence(), ExportedImageMessage::encoded_size,
                  -1);
        return Status::ok;
    }
    if (export_thread_.post(*errand) != Status::ok) errand->conclude();
    return Status::ok;
}

void RendererService::exported(ImageExport &errand, bool ran)
{
    AV_SCOPE("RendererService::exported");
    host_.require_main_loop("RendererService::exported");
    const AvExportedImage &image = errand.image();
    ExportedImageMessage   exported;
    int                    descriptor = -1;
    if (ran && errand.found() && image.fd >= 0 && image.slot < image_slots) {
        AvExportedImage &sent = sent_[image.slot];
        const bool       fresh = sent.fd != image.fd || sent.width != image.width || sent.height != image.height ||
                           sent.stride != image.stride || sent.offset != image.offset || sent.fourcc != image.fourcc ||
                           sent.modifier != image.modifier;
        const ExportedImageMessage described(fresh, image.slot, image.width, image.height, image.stride, image.offset,
                                             image.fourcc, image.modifier);
        if (described.encode(late_ + RendererMessageHeader::encoded_size, ExportedImageMessage::encoded_size) ==
            Status::ok) {
            exported = described;
            descriptor = fresh ? image.fd : -1;
            sent = image;
        }
    }
    static_cast<void>(exported.encode(late_ + RendererMessageHeader::encoded_size, ExportedImageMessage::encoded_size));
    send_late(RendererMessageHeader::Kind::exported_image, errand.sequence(), ExportedImageMessage::encoded_size,
              descriptor);
    static_cast<void>(exports_.destroy(&errand));
}

void RendererService::send_late(RendererMessageHeader::Kind kind, uint64_t sequence, uint32_t length, int descriptor)
{
    AV_SCOPE("RendererService::send_late");
    static_cast<void>(RendererMessageHeader(kind, length, sequence).encode(late_, sizeof late_));
    if (resources_.send_answer(late_, RendererMessageHeader::encoded_size + length, descriptor) != Status::ok)
        host_.log("an answer could not be sent to QEMU");
}

void RendererService::close_all(const int *descriptors, uint32_t count)
{
    AV_SCOPE("RendererService::close_all");
    for (uint32_t index = 0u; index < count; index++)
        if (descriptors != nullptr && descriptors[index] >= 0) resources_.close_descriptor(descriptors[index]);
}

void RendererService::shut_down()
{
    AV_SCOPE("RendererService::shut_down");
    export_thread_.stop();
    memory_thread_.stop();
    plane_.destroy_all();
    interrupts_.disconnect();
    resources_.drop_interrupt_lines();
    blobs_.release_all();
    owners_.forget_all();
    int left = -1;
    while (doorbells_.withdraw(left) == Status::ok) resources_.close_descriptor(left);
}

bool RendererService::ready() const
{
    AV_SCOPE("RendererService::ready");
    return ready_;
}

uint32_t RendererService::outstanding() const
{
    AV_SCOPE("RendererService::outstanding");
    return export_thread_.outstanding() + memory_thread_.outstanding();
}

bool RendererService::invariants_hold() const
{
    AV_SCOPE("RendererService::invariants_hold");
    return plane_.invariants_hold() && blobs_.invariants_hold() && interrupts_.invariants_hold() &&
           memory_.invariants_hold() && doorbells_.invariants_hold() && (ready_ || plane_.live() == 0u);
}

}
