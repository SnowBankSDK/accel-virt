#include "harness/executor/guarded_executor_ops.hpp"

#include "harness/host/mock_host.hpp"

#include <time.h>

namespace av {

namespace {

template <typename F, F AvExecutorOps::*Member>
struct Guarded;

template <typename R, typename... A, R (*AvExecutorOps::*Member)(void *, A...)>
struct Guarded<R (*)(void *, A...), Member> {
    static R call(void *opaque, A... arguments)
    {
        GuardedExecutorOps  &guard = *static_cast<GuardedExecutorOps *>(opaque);
        const AvExecutorOps &inner = guard.inner_table();
        guard.enter(static_cast<std::size_t>(reinterpret_cast<const char *>(&(inner.*Member)) -
                                             reinterpret_cast<const char *>(&inner)));
        return (inner.*Member)(inner.opaque, arguments...);
    }
};

template <typename T>
struct FieldOf;

template <typename F>
struct FieldOf<F AvExecutorOps::*> {
    using type = F;
};

template <auto Member>
constexpr auto guarded = &Guarded<typename FieldOf<decltype(Member)>::type, Member>::call;

}

GuardedExecutorOps::GuardedExecutorOps(MockHost &host, std::uint32_t sub_apis)
    : host_(host), inner_(sub_apis), table_(inner_.table()), lock_(), changed_(), blocking_(false), blocked_(0u)
{
    pthread_mutex_init(&lock_, nullptr);
    pthread_cond_init(&changed_, nullptr);
    table_.opaque = this;
    table_.sub_apis = guarded<&AvExecutorOps::sub_apis>;
    table_.open_domain = guarded<&AvExecutorOps::open_domain>;
    table_.close_domain = guarded<&AvExecutorOps::close_domain>;
    table_.allocate_memory = guarded<&AvExecutorOps::allocate_memory>;
    table_.free_memory = guarded<&AvExecutorOps::free_memory>;
    table_.alias_memory = guarded<&AvExecutorOps::alias_memory>;
    table_.create = guarded<&AvExecutorOps::create>;
    table_.destroy = guarded<&AvExecutorOps::destroy>;
    table_.execute = guarded<&AvExecutorOps::execute>;
    table_.end_batch = guarded<&AvExecutorOps::end_batch>;
    table_.poll = guarded<&AvExecutorOps::poll>;
    table_.gpu_progress = guarded<&AvExecutorOps::gpu_progress>;
    table_.set_signal = guarded<&AvExecutorOps::set_signal>;
    table_.import_blob = guarded<&AvExecutorOps::import_blob>;
    table_.release_blob = guarded<&AvExecutorOps::release_blob>;
    table_.imported_handle = guarded<&AvExecutorOps::imported_handle>;
    table_.share_object = guarded<&AvExecutorOps::share_object>;
    table_.open_object = guarded<&AvExecutorOps::open_object>;
    table_.close_object = guarded<&AvExecutorOps::close_object>;
    table_.release_shared = guarded<&AvExecutorOps::release_shared>;
    table_.read_image = guarded<&AvExecutorOps::read_image>;
    table_.export_image = guarded<&AvExecutorOps::export_image>;
    table_.map_frame = guarded<&AvExecutorOps::map_frame>;
    table_.unmap_frame = guarded<&AvExecutorOps::unmap_frame>;
    table_.export_surface = guarded<&AvExecutorOps::export_surface>;
    table_.sync_surface = guarded<&AvExecutorOps::sync_surface>;
    table_.copy_surface = guarded<&AvExecutorOps::copy_surface>;
    table_.map_surface = guarded<&AvExecutorOps::map_surface>;
    table_.unmap_surface = guarded<&AvExecutorOps::unmap_surface>;
    if (table_.compose_planes != nullptr) table_.compose_planes = guarded<&AvExecutorOps::compose_planes>;
    if (table_.lose_device != nullptr) table_.lose_device = guarded<&AvExecutorOps::lose_device>;
    table_.answer_part = guarded<&AvExecutorOps::answer_part>;
    if (table_.channel_stats != nullptr) table_.channel_stats = guarded<&AvExecutorOps::channel_stats>;
    if (table_.place_memory != nullptr) table_.place_memory = guarded<&AvExecutorOps::place_memory>;
    if (table_.extend_lease != nullptr) table_.extend_lease = guarded<&AvExecutorOps::extend_lease>;
    if (table_.return_lease != nullptr) table_.return_lease = guarded<&AvExecutorOps::return_lease>;
    if (table_.leave_thread != nullptr) table_.leave_thread = guarded<&AvExecutorOps::leave_thread>;
    if (table_.thread_joined != nullptr)
        table_.thread_joined = [](void *opaque, void *left) {
            const AvExecutorOps &inner = static_cast<GuardedExecutorOps *>(opaque)->inner_table();
            inner.thread_joined(inner.opaque, left);
        };
}

GuardedExecutorOps::~GuardedExecutorOps()
{
    unblock();
    pthread_cond_destroy(&changed_);
    pthread_mutex_destroy(&lock_);
}

const AvExecutorOps &GuardedExecutorOps::table() const { return table_; }
MockExecutorOps     &GuardedExecutorOps::inner() { return inner_; }
const AvExecutorOps &GuardedExecutorOps::inner_table() const { return inner_.table(); }

void GuardedExecutorOps::arm() { armed_ = true; }
void GuardedExecutorOps::disarm() { armed_ = false; }

void GuardedExecutorOps::block()
{
    pthread_mutex_lock(&lock_);
    blocking_ = true;
    pthread_mutex_unlock(&lock_);
}

void GuardedExecutorOps::unblock()
{
    pthread_mutex_lock(&lock_);
    blocking_ = false;
    pthread_cond_broadcast(&changed_);
    pthread_mutex_unlock(&lock_);
}

bool GuardedExecutorOps::await_blocked(std::uint32_t calls, std::uint32_t milliseconds)
{
    timespec deadline = {};
    clock_gettime(CLOCK_REALTIME, &deadline);
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(deadline.tv_nsec) + milliseconds * 1000000ull;
    deadline.tv_sec += static_cast<time_t>(nanoseconds / 1000000000ull);
    deadline.tv_nsec = static_cast<long>(nanoseconds % 1000000000ull);
    pthread_mutex_lock(&lock_);
    bool reached = blocked_ >= calls;
    while (!reached && pthread_cond_timedwait(&changed_, &lock_, &deadline) == 0) reached = blocked_ >= calls;
    pthread_mutex_unlock(&lock_);
    return reached;
}

std::uint32_t GuardedExecutorOps::blocked() const
{
    pthread_mutex_lock(const_cast<pthread_mutex_t *>(&lock_));
    const std::uint32_t count = blocked_;
    pthread_mutex_unlock(const_cast<pthread_mutex_t *>(&lock_));
    return count;
}

std::uint32_t GuardedExecutorOps::on_main_loop() const { return on_main_loop_.load(); }
std::uint32_t GuardedExecutorOps::off_main_loop() const { return off_main_loop_.load(); }
std::size_t   GuardedExecutorOps::first_on_main_loop() const { return first_.load(); }

void GuardedExecutorOps::enter(std::size_t operation)
{
    if (!armed_.load()) return;
    if (host_.holds_main_loop()) {
        std::size_t none = 0u;
        first_.compare_exchange_strong(none, operation);
        on_main_loop_++;
        return;
    }
    off_main_loop_++;
    pthread_mutex_lock(&lock_);
    if (blocking_) {
        blocked_++;
        pthread_cond_broadcast(&changed_);
        while (blocking_) pthread_cond_wait(&changed_, &lock_);
        blocked_--;
    }
    pthread_mutex_unlock(&lock_);
}

}
