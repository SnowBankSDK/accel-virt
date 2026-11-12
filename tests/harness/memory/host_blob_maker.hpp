#ifndef AV_HOST_BLOB_MAKER_HPP
#define AV_HOST_BLOB_MAKER_HPP

#include "core/support/status.hpp"
#include "device/control/held_requests.hpp"
#include "device/recording/wire_recorder.hpp"
#include "device/threading/service_thread.hpp"
#include "proto/resources/host_memory_request.hpp"

#include <cstddef>
#include <cstdint>

namespace av {

class ContextPlane;
class HostBlobTable;
class MockHost;
class MockRequest;

class HostBlobMaker {
public:
    static constexpr std::uint32_t patience_ms = 5000u;

    HostBlobMaker(MockHost &host, HostBlobTable &table);
    ~HostBlobMaker();
    HostBlobMaker(const HostBlobMaker &) = delete;
    HostBlobMaker &operator=(const HostBlobMaker &) = delete;

    [[nodiscard]] Status initialize(ContextPlane &plane);
    [[nodiscard]] Status create(std::uint32_t id, std::uint32_t context, HostMemoryRequest request, std::uint64_t size);
    [[nodiscard]] Status adopt(std::uint32_t id, const MockRequest &prototype, std::size_t offset,
                               std::uint32_t entries);
    [[nodiscard]] Status release(std::uint32_t id);
    void                 release_all();
    [[nodiscard]] Status answered(MockRequest &request);
    bool                 settle();

    HeldRequests  &held();
    ServiceThread &service();
    WireRecorder  &recorder();

private:
    MockHost      &host_;
    HostBlobTable &table_;
    WireRecorder   recorder_;
    HeldRequests   held_;
    ServiceThread  service_;
};

}

#endif
