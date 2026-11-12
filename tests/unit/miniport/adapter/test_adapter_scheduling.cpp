#include "miniport/adapter/adapter.hpp"
#include "miniport/display/display_info_response.hpp"
#include "miniport/ddi/dxgk_allocation_info_record.hpp"
#include "miniport/ddi/dxgkarg_create_allocation_record.hpp"
#include "miniport/ddi/dxgkarg_create_context_record.hpp"
#include "miniport/ddi/dxgkarg_create_device_record.hpp"
#include "miniport/ddi/dxgkarg_destroy_allocation_record.hpp"
#include "miniport/ddi/dxgkarg_preempt_command_record.hpp"
#include "miniport/ddi/dxgkarg_submit_command_virtual_record.hpp"
#include "miniport/ddi/dxgkargcb_notify_interrupt_data_record.hpp"
#include "miniport/scheduling/execution_node.hpp"
#include "harness/dxgkrnl/fake_dxgkrnl_adapter.hpp"
#include "harness/dxgkrnl/fake_handle_table.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/platform/mock_platform.hpp"
#include "harness/virtio/fake_virtio_gpu_function.hpp"
#include "harness/virtio/fake_virtqueue_device.hpp"
#include "proto/context/capset_info.hpp"
#include "proto/context/context_setup.hpp"
#include "proto/context/gpu_identity.hpp"
#include "proto/context/protocol_version.hpp"
#include "proto/escape/av_context_record.hpp"
#include "proto/escape/release_record.hpp"
#include "proto/resources/allocation_descriptor.hpp"
#include "proto/submission/fence_page.hpp"
#include "proto/submission/ring_header.hpp"
#include "proto/submission/submission_descriptor.hpp"
#include "proto/virtio_gpu/gpu_response.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <vector>

namespace av {

struct AdapterProbe {
    explicit AdapterProbe(Adapter &target) : adapter(target) {}
    Status   send_without_waiting(const std::uint8_t *request, std::size_t length)
    {
        return adapter.device_.object().send_control(request, length);
    }
    std::uint32_t bound_av_contexts() const { return adapter.av_contexts_.bound(); }
    std::uint32_t live_contexts() const { return adapter.contexts_.live(); }
    std::uint32_t live_devices() const { return adapter.devices_.devices(); }
    std::uint32_t pending_packets() { return adapter.node_.pending(); }
    Adapter &adapter;
};

}

namespace {

constexpr std::uint32_t success = 0u;
constexpr std::uint32_t ctx_create = 0x0200u;
constexpr std::uint32_t ctx_destroy = 0x0201u;
constexpr std::uint32_t submit_3d = 0x0207u;
constexpr std::uint32_t create_blob = 0x010Cu;
constexpr std::uint32_t resource_unref = 0x0102u;
constexpr std::uint32_t map_blob = 0x0208u;
constexpr std::uint32_t get_display_info = 0x0100u;
constexpr std::uint32_t get_capset_info = 0x0108u;
constexpr std::uint32_t get_capset = 0x0109u;
constexpr std::uint32_t resource_flush = 0x0104u;
constexpr std::uint32_t setup_at = 32u;
constexpr std::uint32_t ring_magic = 0x00525641u;
constexpr std::int32_t  dma_completed = 1;
constexpr std::int32_t  dma_preempted = 2;
constexpr std::uint8_t  ring_order = 12u;
constexpr std::uint64_t ring_capacity = std::uint64_t{1} << ring_order;
constexpr std::uint32_t reply_bytes = 4096u;
constexpr std::uint64_t tail_offset = 8u;
constexpr std::uint64_t head_offset = 16u;
constexpr std::uint32_t doorbells = 64u;
constexpr std::uint64_t millisecond = 10000u;
constexpr std::uint64_t timer_slack = 20u * millisecond;
constexpr std::uint32_t most_in_node = 48u;
constexpr std::uint32_t most_live_rings = 20u;
constexpr std::uint32_t most_outstanding_batches = 40u;
constexpr std::uint32_t steps_per_seed = 320u;
constexpr std::uint32_t seeds = 300u;
constexpr std::uint32_t most_processes = 256u;

std::uint32_t nt(std::int32_t status) { return static_cast<std::uint32_t>(status); }

std::uint32_t read_u32(const std::uint8_t *bytes, std::size_t offset)
{
    std::uint32_t value = 0u;
    std::memcpy(&value, bytes + offset, sizeof value);
    return value;
}

class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull) {}
    std::uint32_t below(std::uint32_t bound)
    {
        state_ ^= state_ << 13u;
        state_ ^= state_ >> 7u;
        state_ ^= state_ << 17u;
        return bound == 0u ? 0u : static_cast<std::uint32_t>((state_ >> 11u) % bound);
    }
    bool chance(std::uint32_t percent) { return below(100u) < percent; }

private:
    std::uint64_t state_;
};

class ModelDevice {
public:
    ModelDevice() : random_(1u) {}

    void attach(av::FakeVirtqueueDevice &queue) { queue_ = &queue; }
    void reseed(std::uint32_t seed) { random_ = Random(seed + 77u); }
    void slow_to(std::uint32_t level)
    {
        slowness_ = level;
        countdown_ = 0u;
    }
    void stall()
    {
        if (queue_ == nullptr) return;
        if (countdown_ != 0u) {
            countdown_--;
            return;
        }
        answer_all();
        countdown_ = slowness_ == 0u ? 0u : random_.below(slowness_ * 40u);
    }
    void answer_all()
    {
        if (queue_ != nullptr) static_cast<void>(queue_->answer_all(request_, sizeof request_, &ModelDevice::answer, this));
    }

    std::uint32_t live_contexts() const
    {
        std::uint32_t live = 0u;
        for (const Context &context : contexts_) live += context.live ? 1u : 0u;
        return live;
    }
    std::uint32_t bound_doorbells() const
    {
        std::uint32_t bound = 0u;
        for (std::uint32_t holder : doorbell_holders_) bound += holder != 0u ? 1u : 0u;
        return bound;
    }
    std::uint32_t pinned_resources() const { return static_cast<std::uint32_t>(resources_.size()); }
    std::uint32_t refusals() const { return refusals_; }

private:
    struct Context {
        std::uint32_t id;
        bool          live;
    };

    static std::uint32_t answer(void *self, const std::uint8_t *request, std::uint32_t length, std::uint8_t *reply,
                                std::uint32_t room)
    {
        return static_cast<ModelDevice *>(self)->respond(request, length, reply, room);
    }

    std::uint32_t respond(const std::uint8_t *request, std::uint32_t length, std::uint8_t *reply, std::uint32_t room)
    {
        if (length < 24u) return 0u;
        const std::uint32_t type = read_u32(request, 0u);
        if (type == get_display_info) return display_info(reply, room);
        av::GpuResponse response;
        response.set(av::GpuResponse::ok_nodata);
        if (type == get_capset_info) response.set_capset_info(av::CapsetInfo::capset_id, 2u, av::CapsetInfo::encoded_size);
        if (type == get_capset) response.set_capset(capset());
        if (type == map_blob) response.set_map_info(av::GpuResponse::map_cache_cached);
        if (type == ctx_create && !create(read_u32(request, 16u))) response.set(av::GpuResponse::err_invalid_context_id);
        if (type == ctx_destroy) destroy(read_u32(request, 16u));
        if (type == create_blob && length >= 28u && read_u32(request, 16u) == 0u && !pin(read_u32(request, 24u)))
            response.set(av::GpuResponse::err_invalid_resource_id);
        if (type == resource_unref && length >= 28u) unpin(read_u32(request, 24u));
        if (type == submit_3d && !set_up(request, length)) response.set(av::GpuResponse::err_invalid_parameter);
        if (reply == nullptr) return 0u;
        const std::uint32_t written = room < response.length() ? room : static_cast<std::uint32_t>(response.length());
        std::memcpy(reply, response.bytes(), written);
        return written;
    }

    static av::CapsetInfo capset()
    {
        static constexpr std::uint8_t uuid[av::GpuIdentity::uuid_bytes] = {3};
        return av::CapsetInfo(av::ProtocolVersion(av::ProtocolVersion::current_major, av::ProtocolVersion::current_minor),
                              1u, 64u, 12u, 24u, av::CapsetInfo::feature_host_visible_memory,
                              av::GpuIdentity(1u, 2u, av::GpuIdentity::discrete_gpu, uuid, 0u, 0u, "the host's"));
    }

    static std::uint32_t display_info(std::uint8_t *reply, std::uint32_t room)
    {
        std::uint8_t bytes[av::DisplayInfoResponse::encoded_size] = {};
        const std::uint32_t type = av::DisplayInfoResponse::response_type;
        std::memcpy(bytes, &type, sizeof type);
        const std::uint32_t fields[3] = {1280u, 800u, 1u};
        std::memcpy(bytes + av::DisplayInfoResponse::header_size + 8u, fields, sizeof fields);
        const std::uint32_t written = room < sizeof bytes ? room : static_cast<std::uint32_t>(sizeof bytes);
        if (reply != nullptr) std::memcpy(reply, bytes, written);
        return reply != nullptr ? written : 0u;
    }

    Context *find(std::uint32_t id)
    {
        for (Context &context : contexts_)
            if (context.id == id) return &context;
        return nullptr;
    }

    bool create(std::uint32_t id)
    {
        Context *const known = find(id);
        if (known != nullptr && known->live) {
            refusals_++;
            return false;
        }
        if (known != nullptr) known->live = true;
        else contexts_.push_back(Context{id, true});
        return true;
    }

    void destroy(std::uint32_t id)
    {
        Context *const known = find(id);
        if (known != nullptr) known->live = false;
        for (std::uint32_t &holder : doorbell_holders_)
            if (holder == id) holder = 0u;
    }

    bool pin(std::uint32_t resource)
    {
        for (std::uint32_t held : resources_)
            if (held == resource) return false;
        resources_.push_back(resource);
        return true;
    }

    void unpin(std::uint32_t resource)
    {
        for (std::size_t index = 0u; index < resources_.size(); index++)
            if (resources_[index] == resource) {
                resources_.erase(resources_.begin() + static_cast<std::ptrdiff_t>(index));
                return;
            }
    }

    bool set_up(const std::uint8_t *request, std::uint32_t length)
    {
        if (length < setup_at + av::ContextSetup::encoded_size) return true;
        av::ContextSetup setup;
        if (setup.decode(request + setup_at, av::ContextSetup::encoded_size) != av::Status::ok) return true;
        const std::uint32_t id = read_u32(request, 16u);
        const std::uint32_t doorbell = setup.doorbell();
        if (doorbell >= doorbells) return false;
        if (doorbell_holders_[doorbell] != 0u && doorbell_holders_[doorbell] != id) {
            refusals_++;
            return false;
        }
        doorbell_holders_[doorbell] = id;
        return true;
    }

    av::FakeVirtqueueDevice *queue_ = nullptr;
    Random                   random_;
    std::uint32_t            slowness_ = 0u;
    std::uint32_t            countdown_ = 0u;
    std::vector<Context>     contexts_;
    std::uint32_t            doorbell_holders_[doorbells] = {};
    std::vector<std::uint32_t> resources_;
    std::uint32_t            refusals_ = 0u;
    std::uint8_t             request_[0x10000u] = {};
};

enum class Kind : std::uint8_t { av, route, plain };

struct Coverage {
    std::uint32_t packets = 0u;
    std::uint32_t parked = 0u;
    std::uint32_t forced = 0u;
    std::uint32_t let_go = 0u;
    std::uint32_t returned = 0u;
    std::uint32_t saturated = 0u;
    std::uint32_t saturated_teardowns = 0u;
    std::uint32_t timer_dpcs = 0u;
    std::uint32_t losses = 0u;
};

struct Batch {
    std::uint64_t begin;
    std::uint64_t end;
    std::uint64_t fence;
    std::uint32_t writer;
    std::uint32_t after;
    bool          delivered;
};

struct Packet {
    Kind          kind;
    std::int32_t  context;
    std::int32_t  ring;
    std::int32_t  batch;
    std::uint32_t fence;
    std::uint64_t at;
};

struct KernelContext {
    void             *handle;
    std::uint32_t     process;
    Kind              kind;
    std::int32_t      ring;
    std::uint32_t     writer;
    std::deque<Packet> queue;
    bool              live;
    std::uint32_t     blocked_until;
};

struct Ring {
    std::uint32_t              process;
    std::int32_t               context;
    void                      *allocation;
    std::uint32_t              kmt;
    std::uint32_t              ring_block;
    std::uint32_t              fence_block;
    bool                       by_packet;
    std::vector<std::int32_t>  routes;
    std::vector<Batch>         batches;
    std::uint64_t              written;
    std::uint64_t              tail;
    std::uint64_t              head;
    std::uint64_t              completed;
    std::uint64_t              next_fence;
    std::uint32_t              last_writer;
    bool                       bound;
    bool                       lost;
    bool                       loss_allowed;
};

struct Process {
    av::DxgkargCreateDeviceRecord device;
    bool                          live;
    std::vector<std::int32_t>     contexts;
    std::vector<std::int32_t>     rings;
};

class SchedulingModel {
public:
    SchedulingModel(std::uint32_t seed, Coverage &coverage)
        : coverage_(coverage), random_(seed), dxgkrnl_(platform_, function_), adapter_(platform_, &function_)
    {
        device_.reseed(seed);
        dxgkrnl_.on_interrupt(&SchedulingModel::interrupted, this);
    }
    ~SchedulingModel()
    {
        static_cast<void>(adapter_.stop_device());
        platform_.on_stall(nullptr, nullptr);
    }
    SchedulingModel(const SchedulingModel &) = delete;
    SchedulingModel &operator=(const SchedulingModel &) = delete;

    bool run()
    {
        function_.add_doorbell_region();
        platform_.on_stall(&SchedulingModel::stalled, this);
        std::uint32_t sources = 9u;
        std::uint32_t children = 9u;
        if (nt(adapter_.start_device(nullptr, &dxgkrnl_.record(), &sources, &children)) != success)
            return fail("the adapter did not start");
        const std::uint32_t first = 1u + random_.below(3u);
        for (std::uint32_t index = 0u; index < first && !failed(); index++) create_process();
        for (step_ = 0u; step_ < steps_per_seed && !failed(); step_++) {
            platform_.advance_time(random_.below(static_cast<std::uint32_t>(millisecond)));
            static_cast<void>(platform_.fire_due_timers());
            act();
            if (!failed() && platform_.fire_due_timers() != 0u) run_dpc();
            if (!failed()) maybe_run_dpc();
            if (!failed()) check_step();
        }
        if (!failed()) drain();
        if (!failed()) tear_down();
        if (!failed()) check_empty();
        return !failed();
    }

    const char   *failure() const { return failure_; }
    const char   *detail() const { return detail_; }
    std::uint32_t failed_step() const { return step_; }

private:
    bool failed() const { return failure_[0] != '\0'; }

    bool fail(const char *why)
    {
        if (!failed()) std::snprintf(failure_, sizeof failure_, "%s", why);
        return false;
    }

    static void stalled(void *self)
    {
        auto *const model = static_cast<SchedulingModel *>(self);
        if (!model->control_.has_value()) {
            const av::FakeCommonConfig::Queue &queue = model->function_.common().queue(0u);
            model->control_.emplace(model->platform_, queue.desc, queue.driver, queue.device, queue.size);
            model->device_.attach(*model->control_);
        }
        model->device_.stall();
    }

    static void interrupted(void *self, const av::DxgkargcbNotifyInterruptDataRecord &data)
    {
        static_cast<SchedulingModel *>(self)->reported(data);
    }

    void reported(const av::DxgkargcbNotifyInterruptDataRecord &data)
    {
        if (data.InterruptType == dma_completed) completed(data.FenceId);
        if (data.InterruptType == dma_preempted) preempted(data.Argument1);
    }

    void completed(std::uint32_t fence)
    {
        if (unreported_.empty() || unreported_.front().fence != fence)
            return static_cast<void>(fail("node 0 reported a completion out of submission order"));
        const Packet packet = unreported_.front();
        unreported_.pop_front();
        observe_rings();
        if (!satisfied(packet) && !owner_gone(packet)) coverage_.let_go++;
        if (!failed() && !satisfied(packet) && !excused(packet)) {
            describe(packet);
            fail("a packet completed before the host finished its batch and before its deadline");
        }
    }

    void preempted(std::uint32_t last_completed)
    {
        for (const Packet &packet : unreported_)
            if (packet.fence <= last_completed) return static_cast<void>(fail("a preemption returned a completed packet"));
        coverage_.returned += static_cast<std::uint32_t>(unreported_.size());
        for (auto packet = unreported_.rbegin(); packet != unreported_.rend(); ++packet)
            if (contexts_[static_cast<std::size_t>(packet->context)].live)
                contexts_[static_cast<std::size_t>(packet->context)].queue.push_front(*packet);
        unreported_.clear();
        preempting_ = false;
    }

    Ring &ring_of(const Packet &packet) { return rings_[static_cast<std::size_t>(packet.ring)]; }

    void describe(const Packet &packet)
    {
        const Ring  &ring = ring_of(packet);
        const Batch &batch = ring.batches[static_cast<std::size_t>(packet.batch)];
        std::snprintf(detail_, sizeof detail_,
                      "(packet %u of kind %d, batch %llu [%llu,%llu), ring tail %llu head %llu completed %llu, waited %llu ms, "
                      "%u timers armed)",
                      packet.fence, static_cast<int>(packet.kind), static_cast<unsigned long long>(batch.fence),
                      static_cast<unsigned long long>(batch.begin), static_cast<unsigned long long>(batch.end),
                      static_cast<unsigned long long>(ring.tail), static_cast<unsigned long long>(ring.head),
                      static_cast<unsigned long long>(ring.completed),
                      static_cast<unsigned long long>((platform_.interrupt_time() - packet.at) / millisecond),
                      platform_.armed_timers());
    }

    std::uint8_t *ring_bytes(const Ring &ring) const
    {
        return ring.bound ? platform_.page_block(ring.ring_block) : nullptr;
    }

    std::uint64_t ring_word(const Ring &ring, std::uint64_t offset) const
    {
        const std::uint8_t *const bytes = ring_bytes(ring);
        std::uint64_t             value = 0u;
        if (bytes != nullptr) std::memcpy(&value, bytes + offset, sizeof value);
        return value;
    }

    bool satisfied(const Packet &packet)
    {
        if (packet.kind == Kind::plain) return true;
        const Ring &ring = ring_of(packet);
        return ring.completed >= ring.batches[static_cast<std::size_t>(packet.batch)].fence;
    }

    bool owner_gone(const Packet &packet)
    {
        if (packet.kind == Kind::plain) return false;
        const Ring &ring = ring_of(packet);
        return ring.lost || !ring.bound;
    }

    bool excused(const Packet &packet)
    {
        return owner_gone(packet) || platform_.interrupt_time() - packet.at >= av::ExecutionNode::park_deadline;
    }

    bool waits_on_held(const Packet &packet)
    {
        if (packet.kind == Kind::plain || owner_gone(packet)) return false;
        const Ring &ring = ring_of(packet);
        return ring.tail < ring.batches[static_cast<std::size_t>(packet.batch)].end;
    }

    bool holds_nothing(const Packet &packet)
    {
        if (packet.kind == Kind::plain || owner_gone(packet)) return true;
        const Ring &ring = ring_of(packet);
        for (const Batch &batch : ring.batches)
            if (batch.delivered && ring.tail < batch.end) return false;
        return true;
    }

    void maybe_run_dpc()
    {
        if (dxgkrnl_.dpcs_queued() == dpcs_run_) {
            dpc_waits_ = 0u;
            return;
        }
        if (dpc_waits_++ < 2u && random_.chance(40u)) return;
        run_dpc();
    }

    void run_dpc()
    {
        dpcs_run_ = dxgkrnl_.dpcs_queued();
        dpc_waits_ = 0u;
        platform_.set_irql(av::Irql::dispatch);
        adapter_.dpc_routine();
        platform_.set_irql(av::Irql::passive);
        observe_rings();
        bool blocked = false;
        for (const Packet &packet : unreported_) {
            if (waits_on_held(packet)) {
                blocked = true;
                coverage_.parked++;
                continue;
            }
            if (blocked && satisfied(packet) && holds_nothing(packet) && !owner_gone(packet))
                return static_cast<void>(
                    fail("a finished packet of an owner holding nothing waits behind another owner's held batch"));
        }
    }

    void observe_rings()
    {
        for (Ring &ring : rings_) {
            if (!ring.bound) continue;
            const std::uint64_t tail = ring_word(ring, tail_offset);
            if (tail < ring.tail) return static_cast<void>(fail("a ring's published tail moved back"));
            if (tail != ring.tail) {
                bool boundary = false;
                for (const Batch &batch : ring.batches) boundary = boundary || batch.end == tail;
                if (!boundary || tail > ring.written)
                    return static_cast<void>(fail("a ring's tail was published inside or past its batches"));
                for (const Batch &batch : ring.batches)
                    if (!batch.delivered && batch.end > ring.tail && batch.end <= tail) coverage_.forced++;
                ring.tail = tail;
            }
            const std::uint8_t *const bytes = ring_bytes(ring);
            if (bytes == nullptr || read_u32(bytes, av::RingHeader::lost_at) == 0u) continue;
            if (!ring.lost && !ring.loss_allowed)
                return static_cast<void>(fail("a context was lost though its ring refused nothing and its writers live"));
            if (!ring.lost) coverage_.losses++;
            ring.lost = true;
        }
    }

    void check_step()
    {
        observe_rings();
        const std::uint64_t now = platform_.interrupt_time();
        for (const Packet &packet : unreported_)
            if (waits_on_held(packet) && now - packet.at > av::ExecutionNode::park_deadline + timer_slack) {
                describe(packet);
                return static_cast<void>(fail("a parked packet outlived its deadline"));
            }
        if (platform_.unclaimed_violations() != 0u) fail("the driver broke a platform rule");
        if (!adapter_.invariants_hold()) fail("the adapter's invariants broke");
    }

    void act()
    {
        const std::uint32_t pick = random_.below(100u);
        if (pick < 26u) return write_batch();
        if (pick < 52u) return submit_one();
        if (pick < 58u) return queue_plain();
        if (pick < 72u) return host_step();
        if (pick < 75u) return preempt();
        if (pick < 77u) return idle();
        if (pick < 80u) return device_.slow_to(random_.below(4u));
        if (pick < 83u) return fill_control();
        if (pick < 86u) return catch_up();
        if (pick < 89u) return destroy_route();
        if (pick < 91u) return destroy_ring_allocation();
        if (pick < 93u) return destroy_av_context();
        if (pick < 95u) return static_cast<void>(tear_down_process(pick_process()));
        if (pick < 97u) return create_process();
        host_step();
    }

    std::int32_t pick_process()
    {
        std::vector<std::int32_t> live;
        for (std::size_t index = 0u; index < processes_.size(); index++)
            if (processes_[index].live) live.push_back(static_cast<std::int32_t>(index));
        return live.empty() ? -1 : live[random_.below(static_cast<std::uint32_t>(live.size()))];
    }

    std::uint32_t live_rings() const
    {
        std::uint32_t live = 0u;
        for (const Ring &ring : rings_) live += ring.bound ? 1u : 0u;
        return live;
    }

    void *create_kernel_context(Process &process, const std::uint8_t *record, std::uint32_t size)
    {
        av::DxgkargCreateContextRecord request = {};
        request.Flags = 0x4u;
        request.pPrivateDriverData = const_cast<std::uint8_t *>(record);
        request.PrivateDriverDataSize = size;
        const std::uint32_t status = nt(adapter_.create_context(process.device.hDevice, &request));
        return status == success ? request.hContext : nullptr;
    }

    std::int32_t add_context(std::uint32_t process, void *handle, Kind kind, std::int32_t ring)
    {
        contexts_.push_back(KernelContext{handle, process, kind, ring, kind == Kind::route ? ++writers_ : 0u, {}, true, 0u});
        const auto index = static_cast<std::int32_t>(contexts_.size() - 1u);
        processes_[process].contexts.push_back(index);
        return index;
    }

    void create_process()
    {
        if (live_rings() + 5u > most_live_rings) return;
        processes_.push_back(Process{});
        const auto index = static_cast<std::uint32_t>(processes_.size() - 1u);
        Process   &process = processes_.back();
        process.device.hKmdProcess = &process_names_[index % most_processes];
        if (nt(adapter_.create_device(&process.device)) != success)
            return static_cast<void>(fail("CreateDevice was refused"));
        process.live = true;
        const bool compositor = random_.chance(35u);
        const std::uint32_t count = compositor ? 2u + random_.below(4u) : 1u + random_.below(2u);
        for (std::uint32_t made = 0u; made < count && !failed(); made++)
            create_ring(index, !compositor && random_.chance(75u));
        if (random_.chance(60u) && !failed()) {
            void *const plain = create_kernel_context(processes_[index], nullptr, 0u);
            if (plain == nullptr) return static_cast<void>(fail("a plain kernel context was refused"));
            add_context(index, plain, Kind::plain, -1);
        }
    }

    void create_ring(std::uint32_t process, bool by_packet)
    {
        const av::AllocationDescriptor descriptor(av::AllocationKind::context_ring, true, av::SurfaceLayout(), 0x1000u,
                                                  av::ScanoutTarget());
        std::uint8_t encoded[av::AllocationDescriptor::encoded_size] = {};
        static_cast<void>(descriptor.encode(encoded, sizeof encoded));
        av::DxgkAllocationInfoRecord info = {};
        info.pPrivateDriverData = encoded;
        info.PrivateDriverDataSize = av::AllocationDescriptor::encoded_size;
        av::DxgkargCreateAllocationRecord create = {};
        create.NumAllocations = 1u;
        create.pAllocationInfo = &info;
        if (nt(adapter_.create_allocation(&create)) != success)
            return static_cast<void>(fail("a ring allocation was refused"));
        const std::uint32_t kmt = handles_.insert(info.hAllocation);
        const av::AvContextRecord record(av::SubApi::vulkan, ring_order, kmt, reply_bytes);
        std::uint8_t              bytes[av::AvContextRecord::encoded_size] = {};
        static_cast<void>(record.encode(bytes, sizeof bytes));
        const std::uint32_t made = platform_.page_blocks_made();
        void *const         handle = create_kernel_context(processes_[process], bytes, sizeof bytes);
        if (handle == nullptr) {
            std::snprintf(detail_, sizeof detail_, "(%u refusals by the device, %u rings live)", device_.refusals(), live_rings());
            return static_cast<void>(fail("an av context was refused with doorbells free and the device answering"));
        }
        const std::uint8_t *const ring_page = platform_.page_block(made);
        if (ring_page == nullptr || read_u32(ring_page, 0u) != ring_magic)
            return static_cast<void>(fail("the ring of a new av context was not where the model looks"));
        rings_.push_back(Ring{process, -1, info.hAllocation, kmt, made, made + 1u, by_packet, {}, {}, 0u, 0u, 0u, 0u,
                              1u, 0u, true, false, false});
        const auto ring = static_cast<std::int32_t>(rings_.size() - 1u);
        processes_[process].rings.push_back(ring);
        rings_.back().context = add_context(process, handle, Kind::av, ring);
        write_fence(rings_.back(), 0u);
        if (!by_packet) return;
        const std::uint32_t routes = 1u + random_.below(3u);
        for (std::uint32_t route = 0u; route < routes; route++) {
            void *const plain = create_kernel_context(processes_[process], nullptr, 0u);
            if (plain == nullptr) return static_cast<void>(fail("a release route was refused"));
            const std::int32_t index = add_context(process, plain, Kind::route, ring);
            rings_[static_cast<std::size_t>(ring)].routes.push_back(index);
        }
    }

    void write_fence(Ring &ring, std::uint64_t completed)
    {
        std::uint8_t *const fence = ring.bound ? platform_.page_block(ring.fence_block) : nullptr;
        if (fence != nullptr) static_cast<void>(av::FencePage(completed, completed).encode(fence, av::FencePage::encoded_size));
    }

    std::int32_t pick_ring()
    {
        std::vector<std::int32_t> usable;
        for (std::size_t index = 0u; index < rings_.size(); index++)
            if (rings_[index].bound && !rings_[index].lost) usable.push_back(static_cast<std::int32_t>(index));
        return usable.empty() ? -1 : usable[random_.below(static_cast<std::uint32_t>(usable.size()))];
    }

    void write_batch()
    {
        const std::int32_t picked = pick_ring();
        if (picked < 0) return;
        Ring               &ring = rings_[static_cast<std::size_t>(picked)];
        const std::uint64_t bytes = 32u * (1u + random_.below(8u));
        if (ring.written + bytes - ring.head > ring_capacity) return;
        std::uint32_t outstanding = 0u;
        for (const Batch &batch : ring.batches) outstanding += batch.end > ring.head ? 1u : 0u;
        if (outstanding >= most_outstanding_batches) return;
        std::int32_t context = ring.context;
        std::uint32_t writer = 0u;
        if (ring.by_packet) {
            if (ring.routes.empty()) return;
            context = ring.routes[random_.below(static_cast<std::uint32_t>(ring.routes.size()))];
            writer = contexts_[static_cast<std::size_t>(context)].writer;
        }
        ring.batches.push_back(Batch{ring.written, ring.written + bytes, ring.next_fence++, writer,
                                     ring.by_packet ? ring.last_writer : 0u, false});
        ring.written += bytes;
        if (ring.by_packet) ring.last_writer = writer;
        KernelContext &queued = contexts_[static_cast<std::size_t>(context)];
        queued.queue.push_back(Packet{ring.by_packet ? Kind::route : Kind::av, context, picked,
                                      static_cast<std::int32_t>(ring.batches.size() - 1u), 0u, 0u});
        if (ring.by_packet && random_.chance(35u)) queued.blocked_until = step_ + 3u + random_.below(60u);
    }

    void queue_plain()
    {
        std::vector<std::int32_t> plain;
        for (std::size_t index = 0u; index < contexts_.size(); index++)
            if (contexts_[index].live && contexts_[index].kind == Kind::plain) plain.push_back(static_cast<std::int32_t>(index));
        if (plain.empty()) return;
        const std::int32_t context = plain[random_.below(static_cast<std::uint32_t>(plain.size()))];
        contexts_[static_cast<std::size_t>(context)].queue.push_back(Packet{Kind::plain, context, -1, -1, 0u, 0u});
    }

    void submit_one()
    {
        if (preempting_ || unreported_.size() >= most_in_node) return;
        std::vector<std::int32_t> ready;
        for (std::size_t index = 0u; index < contexts_.size(); index++)
            if (contexts_[index].live && !contexts_[index].queue.empty() &&
                (draining_ || contexts_[index].blocked_until <= step_))
                ready.push_back(static_cast<std::int32_t>(index));
        if (ready.empty()) return;
        KernelContext &context = contexts_[static_cast<std::size_t>(ready[random_.below(static_cast<std::uint32_t>(ready.size()))])];
        Packet packet = context.queue.front();
        context.queue.pop_front();
        submit(context, packet);
    }

    void submit(KernelContext &context, Packet packet)
    {
        std::uint8_t data[av::ReleaseRecord::encoded_size] = {};
        std::uint32_t size = 0u;
        if (packet.kind != Kind::plain) {
            Ring &ring = ring_of(packet);
            if (!ring.bound) return;
            const Batch &batch = ring.batches[static_cast<std::size_t>(packet.batch)];
            const av::SubmissionDescriptor descriptor(batch.fence, batch.begin, batch.end);
            if (packet.kind == Kind::av) {
                static_cast<void>(descriptor.encode(data, sizeof data));
                size = av::SubmissionDescriptor::encoded_size;
            } else {
                static_cast<void>(av::ReleaseRecord(ring.kmt, descriptor, batch.writer, batch.after).encode(data, sizeof data));
                size = av::ReleaseRecord::encoded_size;
            }
        }
        packet.fence = ++fences_;
        coverage_.packets++;
        packet.at = platform_.interrupt_time();
        av::DxgkargSubmitCommandVirtualRecord request = {};
        request.hContext = context.handle;
        request.DmaBufferVirtualAddress = 0x8000000000ull;
        request.DmaBufferSize = 32u;
        request.pDmaBufferPrivateData = size != 0u ? data : nullptr;
        request.DmaBufferPrivateDataSize = size;
        request.SubmissionFenceId = packet.fence;
        unreported_.push_back(packet);
        const std::uint32_t status = nt(adapter_.submit_command_virtual(&request));
        if (status == success) {
            if (packet.kind != Kind::plain) ring_of(packet).batches[static_cast<std::size_t>(packet.batch)].delivered = true;
            return;
        }
        for (auto at = unreported_.begin(); at != unreported_.end(); ++at)
            if (at->fence == packet.fence) {
                unreported_.erase(at);
                break;
            }
        observe_rings();
        if (packet.kind == Kind::plain || !ring_of(packet).lost) fail("SubmitCommandVirtual refused a packet");
    }

    void host_step()
    {
        const std::uint32_t all = random_.chance(30u) ? 1u : 0u;
        const std::int32_t  picked = pick_ring();
        bool                moved = false;
        for (std::size_t index = 0u; index < rings_.size(); index++) {
            if (all == 0u && static_cast<std::int32_t>(index) != picked) continue;
            Ring &ring = rings_[index];
            if (!ring.bound || ring.lost) continue;
            const std::uint64_t tail = ring_word(ring, tail_offset);
            if (tail <= ring.head) continue;
            std::uint64_t head = tail;
            if (random_.chance(30u))
                for (const Batch &batch : ring.batches)
                    if (batch.end > ring.head && batch.end <= tail) {
                        head = batch.end;
                        break;
                    }
            std::uint64_t completed = ring.completed;
            for (const Batch &batch : ring.batches)
                if (batch.end <= head && batch.fence > completed) completed = batch.fence;
            std::uint8_t *const bytes = ring_bytes(ring);
            std::memcpy(bytes + head_offset, &head, sizeof head);
            ring.head = head;
            ring.completed = completed;
            write_fence(ring, completed);
            moved = true;
        }
        if (moved) static_cast<void>(dxgkrnl_.queue_dpc());
    }

    void preempt()
    {
        if (preempting_) return;
        av::DxgkargPreemptCommandRecord request = {};
        request.PreemptionFenceId = ++fences_;
        if (nt(adapter_.preempt_command(&request)) != success) return static_cast<void>(fail("a preemption was refused"));
        preempting_ = true;
    }

    void idle()
    {
        if (dxgkrnl_.dpcs_queued() != dpcs_run_) return;
        const std::uint32_t chunks = 20u + random_.below(200u);
        for (std::uint32_t chunk = 0u; chunk < chunks && !failed(); chunk++) {
            platform_.advance_time(10u * millisecond);
            static_cast<void>(platform_.fire_due_timers());
            if (dxgkrnl_.dpcs_queued() != dpcs_run_) {
                coverage_.timer_dpcs++;
                run_dpc();
            }
            if (!failed()) check_step();
        }
    }

    void fill_control()
    {
        std::uint8_t request[32] = {};
        std::memcpy(request, &resource_flush, sizeof resource_flush);
        for (std::uint32_t sent = 0u; sent < 24u; sent++)
            if (av::AdapterProbe(adapter_).send_without_waiting(request, sizeof request) != av::Status::ok) {
                coverage_.saturated++;
                saturated_ = true;
                return;
            }
    }

    void catch_up()
    {
        device_.answer_all();
        saturated_ = false;
        static_cast<void>(dxgkrnl_.queue_dpc());
    }

    bool submitted_by(std::int32_t context) const
    {
        for (const Packet &packet : unreported_)
            if (packet.context == context) return true;
        return false;
    }

    std::int32_t pick_context(Kind kind)
    {
        std::vector<std::int32_t> candidates;
        for (std::size_t index = 0u; index < contexts_.size(); index++)
            if (contexts_[index].live && contexts_[index].kind == kind) candidates.push_back(static_cast<std::int32_t>(index));
        return candidates.empty() ? -1 : candidates[random_.below(static_cast<std::uint32_t>(candidates.size()))];
    }

    bool destroy_context(std::int32_t index)
    {
        KernelContext &context = contexts_[static_cast<std::size_t>(index)];
        if (submitted_by(index)) return false;
        for (const Packet &packet : context.queue)
            if (packet.kind == Kind::route && !ring_of(packet).batches[static_cast<std::size_t>(packet.batch)].delivered)
                ring_of(packet).loss_allowed = true;
        context.queue.clear();
        context.live = false;
        if (context.kind == Kind::route) {
            Ring &ring = rings_[static_cast<std::size_t>(context.ring)];
            for (std::size_t route = 0u; route < ring.routes.size(); route++)
                if (ring.routes[route] == index) {
                    ring.routes.erase(ring.routes.begin() + static_cast<std::ptrdiff_t>(route));
                    break;
                }
        }
        if (context.kind == Kind::av && rings_[static_cast<std::size_t>(context.ring)].bound && saturated_)
            coverage_.saturated_teardowns++;
        if (context.kind == Kind::av) forget_ring(rings_[static_cast<std::size_t>(context.ring)]);
        if (nt(adapter_.destroy_context(context.handle)) != success) return fail("DestroyContext was refused");
        observe_rings();
        return !failed();
    }

    void forget_ring(Ring &ring)
    {
        ring.bound = false;
        for (KernelContext &context : contexts_)
            for (auto packet = context.queue.begin(); packet != context.queue.end();)
                packet = packet->ring >= 0 && &ring_of(*packet) == &ring ? context.queue.erase(packet) : packet + 1;
    }

    void destroy_route()
    {
        const std::int32_t route = pick_context(Kind::route);
        if (route >= 0 && contexts_[static_cast<std::size_t>(route)].queue.empty())
            static_cast<void>(destroy_context(route));
    }

    void destroy_av_context()
    {
        const std::int32_t context = pick_context(Kind::av);
        if (context >= 0) static_cast<void>(destroy_context(context));
    }

    bool destroy_allocation(Ring &ring)
    {
        if (ring.allocation == nullptr) return true;
        forget_ring(ring);
        av::DxgkargDestroyAllocationRecord destroy = {};
        destroy.NumAllocations = 1u;
        destroy.pAllocationList = &ring.allocation;
        const std::uint32_t status = nt(adapter_.destroy_allocation(&destroy));
        ring.allocation = nullptr;
        return status == success || fail("DestroyAllocation of a ring was refused");
    }

    void destroy_ring_allocation()
    {
        const std::int32_t ring = pick_ring();
        if (ring >= 0 && random_.chance(50u)) static_cast<void>(destroy_allocation(rings_[static_cast<std::size_t>(ring)]));
    }

    bool tear_down_process(std::int32_t index)
    {
        if (index < 0) return true;
        Process &process = processes_[static_cast<std::size_t>(index)];
        for (std::int32_t context : process.contexts)
            if (contexts_[static_cast<std::size_t>(context)].live && submitted_by(context)) return false;
        for (Kind kind : {Kind::route, Kind::plain, Kind::av})
            for (std::int32_t context : process.contexts)
                if (contexts_[static_cast<std::size_t>(context)].live && contexts_[static_cast<std::size_t>(context)].kind == kind &&
                    !destroy_context(context))
                    return false;
        for (std::int32_t ring : process.rings)
            if (!destroy_allocation(rings_[static_cast<std::size_t>(ring)])) return false;
        if (nt(adapter_.destroy_device(process.device.hDevice)) != success) return fail("DestroyDevice was refused");
        process.live = false;
        return true;
    }

    bool settled() const
    {
        if (!unreported_.empty() || preempting_) return false;
        for (const KernelContext &context : contexts_)
            if (context.live && !context.queue.empty()) return false;
        return true;
    }

    void drain()
    {
        device_.slow_to(0u);
        draining_ = true;
        for (std::uint32_t round = 0u; round < 4000u && !failed() && !settled(); round++) {
            platform_.advance_time(millisecond);
            static_cast<void>(platform_.fire_due_timers());
            submit_one();
            host_step();
            if (dxgkrnl_.dpcs_queued() != dpcs_run_) run_dpc();
            if (!failed()) check_step();
        }
        if (failed()) return;
        if (!settled()) return static_cast<void>(fail("the node never settled with nothing left to send"));
        for (const Ring &ring : rings_) {
            if (!ring.bound || ring.lost) continue;
            for (const Batch &batch : ring.batches)
                if (batch.delivered && ring.tail < batch.end)
                    return static_cast<void>(fail("a delivered batch of a live ring was never published"));
        }
    }

    void tear_down()
    {
        for (std::size_t index = 0u; index < processes_.size() && !failed(); index++)
            if (processes_[index].live && !tear_down_process(static_cast<std::int32_t>(index)))
                fail("a settled process could not be torn down");
        for (std::uint32_t round = 0u; round < 4u; round++) {
            device_.answer_all();
            run_dpc();
        }
    }

    void check_empty()
    {
        av::AdapterProbe probe(adapter_);
        if (device_.live_contexts() != 0u) return static_cast<void>(fail("the device still holds a live context"));
        if (device_.bound_doorbells() != 0u) return static_cast<void>(fail("the device still holds a bound doorbell"));
        if (device_.pinned_resources() != 0u) return static_cast<void>(fail("the device still holds pinned guest pages"));
        if (device_.refusals() != 0u) return static_cast<void>(fail("the device refused a context or doorbell in use"));
        if (probe.bound_av_contexts() != 0u || probe.live_contexts() != 0u || probe.live_devices() != 0u ||
            probe.pending_packets() != 0u)
            return static_cast<void>(fail("the kernel tables are not empty after everything was destroyed"));
        if (platform_.armed_timers() != 0u) return static_cast<void>(fail("a park timer is still armed"));
        if (platform_.unclaimed_violations() != 0u) fail("the driver broke a platform rule");
    }

    Coverage                              &coverage_;
    Random                                 random_;
    av::MockPlatform                       platform_;
    av::FakeVirtioGpuFunction              function_;
    av::FakeDxgkrnlAdapter                 dxgkrnl_;
    av::Adapter                            adapter_;
    av::FakeHandleTable                    handles_;
    ModelDevice                            device_;
    std::optional<av::FakeVirtqueueDevice> control_;
    std::vector<Process>                   processes_;
    std::vector<KernelContext>             contexts_;
    std::vector<Ring>                      rings_;
    std::deque<Packet>                     unreported_;
    std::uint8_t                           process_names_[most_processes] = {};
    std::uint32_t                          fences_ = 0u;
    std::uint32_t                          writers_ = 0u;
    std::uint32_t                          dpcs_run_ = 0u;
    std::uint32_t                          dpc_waits_ = 0u;
    std::uint32_t                          step_ = 0u;
    bool                                   preempting_ = false;
    bool                                   saturated_ = false;
    bool                                   draining_ = false;
    char                                   failure_[160] = {};
    char                                   detail_[256] = {};
};

}

TEST(adapter_scheduling, seeded_interleavings_of_processes_contexts_and_a_slow_device_keep_every_scheduling_contract)
{
    std::uint32_t failures = 0u;
    Coverage      coverage;
    const char *const only = std::getenv("AV_MODEL_SEED");
    for (std::uint32_t seed = 1u; seed <= seeds && failures < 3u; seed++) {
        if (only != nullptr && seed != static_cast<std::uint32_t>(std::atoi(only))) continue;
        auto *const model = new SchedulingModel(seed, coverage);
        if (!model->run()) {
            failures++;
            ::av::TestRegistry::instance().record_failure(__FILE__, __LINE__, "seed %u step %u: %s %s", seed,
                                                          model->failed_step(), model->failure(), model->detail());
        }
        delete model;
    }
    if (only != nullptr) return;
    CHECK(coverage.packets > 10000u);
    CHECK(coverage.parked > 0u);
    CHECK(coverage.forced > 0u);
    CHECK(coverage.let_go > 0u);
    CHECK(coverage.returned > 0u);
    CHECK(coverage.saturated_teardowns > 0u);
    CHECK(coverage.timer_dpcs > 0u);
    CHECK(coverage.losses > 0u);
}
