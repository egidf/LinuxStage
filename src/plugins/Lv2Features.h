// Lv2Features.h - the set of LV2 features this host provides.
//
// Shared by Lv2Instance (instantiation gate) and Lv2Host (scan-time
// "Unsupported" classification) so the two can never disagree: a plugin whose
// required features pass scan() must also pass instantiate().
#pragma once

#include <string>

#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

namespace lspa {

// True when this host provides `uri` to DSP instances. Anything else that a
// plugin declares as *required* makes it Unsupported (reported via errorOut
// and PluginDescriptor::warning as "requires unsupported feature: <uri>").
inline bool lv2FeatureProvided(const std::string& uri) {
    static const char* kProvided[] = {
        LV2_URID__map,
        LV2_URID__unmap,
        LV2_BUF_SIZE__boundedBlockLength,
        LV2_BUF_SIZE__powerOf2BlockLength,
        LV2_BUF_SIZE__fixedBlockLength,
        LV2_OPTIONS__options,
        LV2_WORKER__schedule,
        // Informational core features accepted by reference hosts (jalv).
        "http://lv2plug.in/ns/lv2core#inPlaceBroken",
        "http://lv2plug.in/ns/lv2core#isLive",
    };
    for (const char* f : kProvided) {
        if (uri == f) return true;
    }
    return false;
}

}  // namespace lspa
