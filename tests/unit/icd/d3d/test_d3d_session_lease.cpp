#include "core/support/status.hpp"
#include "guest/transport/guest_connection.hpp"
#include "guest/transport/kmt_transport.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/guest/mock_kmt.hpp"
#include "harness/icd/lock_trace.hpp"
#include "harness/icd/recording_thread_lock.hpp"
#include "icd/d3d/d3d_command_stream.hpp"
#include "icd/d3d/d3d_device_channel.hpp"
#include "icd/d3d/d3d_session_lease.hpp"
#include "proto/context/sub_api.hpp"
#include "proto/generated/d3d/d3d_dispatch_client.h"
#include "proto/generated/d3d/d3d_protocol_ids.h"
#include "proto/generated/d3d9/d3d9_dispatch_client.h"
#include "proto/generated/d3d9/d3d9_protocol_ids.h"
#include "proto/submission/command_header.hpp"
#include "proto/submission/lease_record.hpp"

#include <cstdint>
#include <cstring>

using av::CommandHeader;
using av::GuestConnection;
using av::KmtTransport;
using av::LeaseRecord;
using av::LockTrace;
using av::MockKmt;
using av::PciIdentity;
using av::RecordingThreadLock;
using av::Status;
using av::SubApi;
using av::d3d::D3dCommandStream;
using av::d3d::D3dDeviceChannel;
using av::d3d::D3dSessionLease;

namespace {

const PciIdentity       avgpu(0x1AF4u, 0x1050u, 0x1AF4u, 0x5256u);
constexpr std::size_t   staging_bytes = 1024u;
constexpr std::size_t   block_bytes = 8192u;
constexpr std::uint32_t first = 1u;
constexpr std::uint32_t second = 2u;
constexpr std::uint64_t device_bytes = std::uint64_t{16} << 20u;

enum class Api { direct3d9, direct3d11 };

class Fixture {
public:
    Fixture(Api api, const std::uint64_t *initial)
        : api_(api), connection_(kmt_), transport_(connection_, SubApi::d3d, 16u, 4096u, staging_, staging_bytes),
          stream_(transport_), lock_(trace_, 'S'), lease_(stream_, lock_, initial), first_(stream_, lock_, first),
          second_(stream_, lock_, second)
    {
        kmt_.add_adapter(avgpu, 3200u);
        std::uint32_t adapters[1] = {};
        std::uint32_t count = 0u;
        static_cast<void>(kmt_.enumerate_adapters(adapters, 1u, count));
        CHECK_STATUS(connection_.open(adapters[0]), Status::ok);
        CHECK_STATUS(transport_.initialize(), Status::ok);
        transport_.hold_in(block_, block_bytes);
        if (api_ == Api::direct3d9) stream_.speak_with(av::d3d9proto::kCmd_SelectDevice9, av::d3d9proto::kCmd_Flush9);
        else stream_.speak_with(av::d3dproto::kCmd_SelectDevice11, av::d3dproto::kCmd_Flush11);
    }
    ~Fixture() { transport_.teardown(); }

    MockKmt          &kmt() { return kmt_; }
    D3dCommandStream &stream() { return stream_; }
    D3dSessionLease  &lease() { return lease_; }
    D3dDeviceChannel &first_device() { return first_; }
    D3dDeviceChannel &second_device() { return second_; }
    RecordingThreadLock &lock() { return lock_; }

    std::uint32_t select_id() const
    {
        return api_ == Api::direct3d9 ? av::d3d9proto::kCmd_SelectDevice9 : av::d3dproto::kCmd_SelectDevice11;
    }
    std::uint32_t create_id() const
    {
        return api_ == Api::direct3d9 ? av::d3d9proto::kCmd_CreateDevice9 : av::d3dproto::kCmd_CreateDevice11;
    }

    void grant(std::uint64_t bytes)
    {
        std::uint8_t answer[LeaseRecord::answer_size] = {};
        std::memcpy(answer, &bytes, sizeof bytes);
        kmt_.queue_answer(answer, sizeof answer);
    }

    bool open(D3dDeviceChannel &device)
    {
        std::uint32_t answer[2] = {0u, 0xb000u};
        kmt_.queue_answer(reinterpret_cast<const std::uint8_t *>(answer),
                          api_ == Api::direct3d9 ? sizeof(std::uint32_t) : sizeof answer);
        std::uint32_t hresult = 1u, level = 0u;
        const bool sent = api_ == Api::direct3d9 ? device.open9(hresult) : device.open(0xb000u, hresult, level);
        return sent && hresult == 0u;
    }

    bool command(D3dDeviceChannel &device)
    {
        return api_ == Api::direct3d9 ? av::d3d9proto::av_d3d9_SetIndices9(device, 7u)
                                      : av::d3dproto::av_d3d_Draw(device, 3u, 0u);
    }

    bool close(D3dDeviceChannel &device) { return api_ == Api::direct3d9 ? device.close9() : device.close(); }

    std::uint32_t find(std::uint32_t id, std::uint32_t from = 0u) const
    {
        for (std::uint32_t index = from; index < kmt_.commands(); index++)
            if (kmt_.command_id(index) == id) return index;
        return kmt_.commands();
    }

    std::uint32_t selected_at(std::uint32_t index) const
    {
        std::uint32_t device = 0u;
        if (kmt_.command_size(index) >= CommandHeader::encoded_size + sizeof device)
            std::memcpy(&device, kmt_.command_bytes(index) + CommandHeader::encoded_size, sizeof device);
        return device;
    }

private:
    Api                 api_;
    MockKmt             kmt_;
    GuestConnection     connection_;
    std::uint8_t        staging_[staging_bytes] = {};
    KmtTransport        transport_;
    std::uint8_t        block_[block_bytes] = {};
    D3dCommandStream    stream_;
    LockTrace           trace_;
    RecordingThreadLock lock_;
    D3dSessionLease     lease_;
    D3dDeviceChannel    first_;
    D3dDeviceChannel    second_;
};

void a_second_device_extends_the_lease_before_it_exists_without_naming_itself(Api api)
{
    Fixture fixture(api, LeaseRecord::direct3d_initial_bytes);
    CHECK(fixture.lease().take(device_bytes));
    CHECK(fixture.open(fixture.first_device()));
    CHECK(fixture.command(fixture.first_device()));
    CHECK_EQ(fixture.lease().extensions(), 0u);

    fixture.grant(device_bytes);
    CHECK(fixture.lease().take(device_bytes));
    CHECK_EQ(fixture.lease().extensions(), 1u);
    CHECK(fixture.open(fixture.second_device()));
    CHECK(fixture.command(fixture.second_device()));
    CHECK(fixture.command(fixture.first_device()));
    CHECK_STATUS(fixture.stream().flush(), Status::ok);
    CHECK(!fixture.lock().held());

    const std::uint32_t extended = fixture.find(CommandHeader::extend_lease_id);
    const std::uint32_t created = fixture.find(fixture.create_id(), extended);
    CHECK(extended < created);
    CHECK(created < fixture.kmt().commands());
    CHECK_EQ(fixture.find(fixture.select_id()), fixture.find(fixture.select_id(), created + 1u));
    const std::uint32_t reselected = fixture.find(fixture.select_id(), created);
    CHECK(reselected < fixture.kmt().commands());
    CHECK_EQ(fixture.selected_at(reselected), first);
    CHECK_EQ(fixture.find(fixture.select_id(), reselected + 1u), fixture.kmt().commands());
    CHECK(fixture.stream().invariants_hold());
}

void a_closed_devices_lease_goes_back_without_naming_it(Api api)
{
    const std::uint64_t initial[LeaseRecord::kinds] = {4u * device_bytes, 0u};
    Fixture fixture(api, initial);
    CHECK(fixture.lease().take(device_bytes));
    CHECK(fixture.open(fixture.first_device()));
    CHECK(fixture.lease().take(device_bytes));
    CHECK(fixture.open(fixture.second_device()));
    CHECK(fixture.command(fixture.second_device()));
    CHECK(fixture.close(fixture.second_device()));
    const std::uint32_t closed = fixture.kmt().commands();
    fixture.lease().give(device_bytes);
    CHECK_STATUS(fixture.stream().flush(), Status::ok);
    const std::uint32_t returned = fixture.find(CommandHeader::return_lease_id);
    CHECK(returned < fixture.kmt().commands());
    CHECK_EQ(fixture.find(fixture.select_id(), closed), fixture.kmt().commands());
    CHECK(fixture.command(fixture.first_device()));
    CHECK_STATUS(fixture.stream().flush(), Status::ok);
    const std::uint32_t reselected = fixture.find(fixture.select_id(), returned);
    CHECK(reselected < fixture.kmt().commands());
    CHECK_EQ(fixture.selected_at(reselected), first);
    CHECK(!fixture.lock().held());
}

}

TEST(d3d_session_lease, a_second_direct3d_9_device_extends_the_lease_before_it_exists_without_naming_itself)
{
    a_second_device_extends_the_lease_before_it_exists_without_naming_itself(Api::direct3d9);
}

TEST(d3d_session_lease, a_second_direct3d_11_device_extends_the_lease_before_it_exists_without_naming_itself)
{
    a_second_device_extends_the_lease_before_it_exists_without_naming_itself(Api::direct3d11);
}

TEST(d3d_session_lease, a_closed_direct3d_9_devices_lease_goes_back_without_naming_it)
{
    a_closed_devices_lease_goes_back_without_naming_it(Api::direct3d9);
}

TEST(d3d_session_lease, a_closed_direct3d_11_devices_lease_goes_back_without_naming_it)
{
    a_closed_devices_lease_goes_back_without_naming_it(Api::direct3d11);
}

TEST(d3d_session_lease, a_refused_extension_takes_nothing_and_sends_no_device_command)
{
    Fixture fixture(Api::direct3d9, LeaseRecord::direct3d_initial_bytes);
    CHECK(fixture.lease().take(device_bytes));
    CHECK(fixture.open(fixture.first_device()));
    fixture.grant(0u);
    CHECK(!fixture.lease().take(device_bytes));
    CHECK_EQ(fixture.lease().left(), 0u);
    CHECK_STATUS(fixture.stream().flush(), Status::ok);
    CHECK_EQ(fixture.find(fixture.select_id()), fixture.kmt().commands());
    CHECK_EQ(fixture.kmt().command_id(fixture.kmt().commands() - 1u), CommandHeader::extend_lease_id);
    CHECK(!fixture.lock().held());
}
