// UridMap.h - LV2 URID map implementation (lv2:urid#map/unmap).
//
// One shared instance is created per Lv2Host and handed to every instance it
// creates, so URIDs are consistent across DSP instances and their UIs (the
// LV2 spec requires a single mapper per host).
//
// Thread safety: map()/unmap() take a lock. Mapping is a non-real-time
// operation (LV2 puts urid:map in the "instantiation" threading class); the
// host pre-maps every URI it needs during process() up front.
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

#include <lv2/urid/urid.h>

namespace lspa {

// Minimal URID mapper shared by all instances of one Lv2Host.
class UridMap {
public:
    UridMap();

    UridMap(const UridMap&) = delete;
    UridMap& operator=(const UridMap&) = delete;

    // Returns a non-zero URID for uri (creating one on first use).
    uint32_t map(const char* uri);
    uint32_t map(const std::string& uri) { return map(uri.c_str()); }
    const char* unmap(uint32_t urid) const;

    // Feature structs with stable addresses for the lifetime of this object.
    // The handle field points back at this UridMap.
    const LV2_URID_Map* mapFeature() const { return &mapFeature_; }
    const LV2_URID_Unmap* unmapFeature() const { return &unmapFeature_; }

private:
    static LV2_URID mapTrampoline(LV2_URID_Map_Handle handle, const char* uri);
    static const char* unmapTrampoline(LV2_URID_Unmap_Handle handle, uint32_t urid);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, uint32_t> map_;
    // URIs by URID (index urid-1). std::deque keeps element addresses stable
    // so unmap() never returns a pointer invalidated by later insertions.
    std::deque<std::string> unmapTable_;
    LV2_URID_Map mapFeature_{};
    LV2_URID_Unmap unmapFeature_{};
};

}  // namespace lspa
