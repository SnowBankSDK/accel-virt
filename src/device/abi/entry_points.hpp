#ifndef AV_DEVICE_ENTRY_POINTS_HPP
#define AV_DEVICE_ENTRY_POINTS_HPP

#include "device/abi/av_device.h"

namespace av {

class DeviceModule;

extern constinit DeviceModule device_module;

}

extern "C" {

void AvWorkerRun(void *worker);
void AvWorkerReaperRun(void *reaper);
void AvContextsReaped(void *table);
void AvRingAttach(void *ring);
void AvDisplayClockRun(void *clock);
void AvDevicePulseRun(void *pulse);
void AvDisplayShowRun(void *show);
void AvDisplayFramesExport(void *device);
void AvDisplayImagesShow(void *device);
void AvDisplayFramesShow(void *device);
void AvPresentEngineRun(void *engine);
void AvPresentEngineReleased(void *engine);
void AvExportConnectionServe(void *connection);
void AvServiceThreadRun(void *service);
void AvServiceThreadConclude(void *service);
void AvErrandPerform(void *errand);
void AvErrandConclude(void *errand);

}

#endif
