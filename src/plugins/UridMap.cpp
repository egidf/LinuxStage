#include "plugins/UridMap.h"

namespace lspa {

UridMap::UridMap() {
    mapFeature_.handle = this;
    mapFeature_.map = &UridMap::mapTrampoline;
    unmapFeature_.handle = this;
    unmapFeature_.unmap = &UridMap::unmapTrampoline;
}

uint32_t UridMap::map(const char* uri) {
    if (uri == nullptr || uri[0] == '\0') return 0;  // 0 is never a valid URID
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(uri);
    if (it != map_.end()) return it->second;
    // URIDs are 1-based; 0 means "not mapped".
    const uint32_t urid = static_cast<uint32_t>(unmapTable_.size() + 1);
    unmapTable_.emplace_back(uri);
    map_.emplace(unmapTable_.back(), urid);
    return urid;
}

const char* UridMap::unmap(uint32_t urid) const {
    if (urid == 0) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t idx = static_cast<size_t>(urid) - 1;
    if (idx >= unmapTable_.size()) return nullptr;
    return unmapTable_[idx].c_str();
}

LV2_URID UridMap::mapTrampoline(LV2_URID_Map_Handle handle, const char* uri) {
    return static_cast<UridMap*>(handle)->map(uri);
}

const char* UridMap::unmapTrampoline(LV2_URID_Unmap_Handle handle, uint32_t urid) {
    return static_cast<UridMap*>(handle)->unmap(urid);
}

}  // namespace lspa
