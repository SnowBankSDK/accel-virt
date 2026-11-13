#ifndef AV_LIFECYCLE_GATES_H
#define AV_LIFECYCLE_GATES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AvLifecycleGates {
    uint32_t size;
    void    *self;
    void    *(*gate_of)(void *self, const uint8_t *device_uuid);
    int      (*enter)(void *gate);
    void     (*leave)(void *gate);
    int      (*share)(void *gate);
    void     (*unshare)(void *gate);
};

#ifdef __cplusplus
}
#endif

#endif
