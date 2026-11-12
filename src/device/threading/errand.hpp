#ifndef AV_ERRAND_HPP
#define AV_ERRAND_HPP

#include "device/threading/work_item.hpp"

namespace av {

class Errand {
public:
    Errand(const Errand &) = delete;
    Errand &operator=(const Errand &) = delete;

    WorkItem item();
    void     perform();
    void     conclude();
    void     run_here();
    void     detach();
    bool     detached() const;
    Errand  *next() const;
    void     follow(Errand *next);

protected:
    Errand();
    ~Errand() = default;

    virtual void run() = 0;
    virtual void finish(bool ran) = 0;

private:
    Errand *next_;
    bool    ran_;
    bool    detached_;
};

}

#endif
