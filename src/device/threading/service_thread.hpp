#ifndef AV_SERVICE_THREAD_HPP
#define AV_SERVICE_THREAD_HPP

#include "core/support/status.hpp"

#include <stdint.h>

namespace av {

class Errand;
class Host;
struct ServiceThreadProbe;

class ServiceThread {
public:
    static constexpr uint64_t stop_budget_nanoseconds = 2000000000u;

    explicit ServiceThread(Host &host);
    ~ServiceThread();
    ServiceThread(const ServiceThread &) = delete;
    ServiceThread &operator=(const ServiceThread &) = delete;

    [[nodiscard]] Status start();
    [[nodiscard]] Status post(Errand &errand);
    void                 stop();
    bool                 running() const;
    uint32_t             outstanding() const;
    void                 run();
    void                 conclude_done();

    bool invariants_hold() const;

private:
    struct Chain {
        Errand *first_;
        Errand *last_;
    };

    static void     append(Chain &chain, Errand &errand);
    static Errand  *take(Chain &chain);
    Errand         *next_queued();
    void            finished(Errand &errand);
    void            release();

    Host     &host_;
    void     *thread_;
    void     *wake_;
    void     *exited_;
    void     *lock_;
    Chain     queued_;
    Chain     done_;
    uint32_t  outstanding_;
    bool      stopping_;
    bool      concluding_;
    bool      abandoned_;

    friend struct ServiceThreadProbe;
};

}

#endif
