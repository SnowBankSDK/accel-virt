#ifndef AV_TEARDOWN_ORDER_H
#define AV_TEARDOWN_ORDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AvTeardownOrder {
    uint32_t size;
    void    *self;
    int      (*needed_by)(void *self, uint32_t driver_id);
    void     (*compiling)(void *self);
    void     (*compiled)(void *self);
    void     (*destroying)(void *self);
    void     (*destroyed)(void *self);
};

#ifdef __cplusplus
}
#endif

#endif
