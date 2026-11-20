#include "device/threading/errand.hpp"

#include "core/trace/function_scope.hpp"
#include "device/abi/entry_points.hpp"

namespace av {

Errand::Errand() : next_(nullptr), ran_(false), detached_(false)
{
    AV_SCOPE("Errand::Errand");
}

WorkItem Errand::item()
{
    AV_SCOPE("Errand::item");
    return WorkItem(&AvErrandPerform, &AvErrandConclude, this);
}

void Errand::perform()
{
    AV_SCOPE("Errand::perform");
    run();
    ran_ = true;
}

void Errand::conclude()
{
    AV_SCOPE("Errand::conclude");
    finish(ran_);
}

void Errand::run_here()
{
    AV_SCOPE("Errand::run_here");
    perform();
    conclude();
}

void Errand::detach()
{
    AV_SCOPE("Errand::detach");
    detached_ = true;
}

bool Errand::detached() const
{
    AV_SCOPE("Errand::detached");
    return detached_;
}

Errand *Errand::next() const
{
    AV_SCOPE("Errand::next");
    return next_;
}

void Errand::follow(Errand *next)
{
    AV_SCOPE("Errand::follow");
    next_ = next;
}

}
