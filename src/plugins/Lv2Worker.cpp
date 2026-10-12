#include "plugins/Lv2Worker.h"

#include <chrono>
#include <cstring>

#include "core/Logger.h"

namespace lspa {

// ---------------------------------------------------------------------------
// Lv2PacketRing
// ---------------------------------------------------------------------------

Lv2PacketRing::Lv2PacketRing(uint32_t capacity) : capacity_(capacity), mask_(capacity - 1) {
    // Round up to a power of two so index arithmetic can use a mask.
    uint32_t pow2 = 1;
    while (pow2 < capacity_) pow2 <<= 1;
    capacity_ = pow2;
    mask_ = capacity_ - 1;
    buf_.resize(capacity_, 0);
}

namespace {

// Copy to/from a free-running byte offset, splitting across the wrap point.
// Position arithmetic works modulo 2^32; capacity is a power of two divisor
// of 2^32 so `pos & mask` is always the correct physical index.
inline void copyOut(const std::vector<uint8_t>& buf, uint32_t mask, uint32_t pos,
                    void* dst, uint32_t len) {
    const uint32_t idx = pos & mask;
    const uint32_t first = (len < buf.size() - idx) ? len : static_cast<uint32_t>(buf.size() - idx);
    std::memcpy(dst, buf.data() + idx, first);
    if (first < len) std::memcpy(static_cast<uint8_t*>(dst) + first, buf.data(), len - first);
}

inline void copyIn(std::vector<uint8_t>& buf, uint32_t mask, uint32_t pos,
                   const void* src, uint32_t len) {
    const uint32_t idx = pos & mask;
    const uint32_t first = (len < buf.size() - idx) ? len : static_cast<uint32_t>(buf.size() - idx);
    std::memcpy(buf.data() + idx, src, first);
    if (first < len) std::memcpy(buf.data(), static_cast<const uint8_t*>(src) + first, len - first);
}

}  // namespace

bool Lv2PacketRing::write(uint32_t size, const void* data) {
    if (size == 0 || data == nullptr) return false;
    const uint32_t w = head_.load(std::memory_order_relaxed);  // producer-owned cursor
    const uint32_t r = tail_.load(std::memory_order_acquire);
    if (capacity_ - (w - r) < 4u + size) return false;  // not enough room
    copyIn(buf_, mask_, w, &size, 4);
    copyIn(buf_, mask_, w + 4, data, size);
    head_.store(w + 4u + size, std::memory_order_release);
    return true;
}

uint32_t Lv2PacketRing::read(void* dst, uint32_t bufSize) {
    const uint32_t r = tail_.load(std::memory_order_relaxed);  // consumer-owned cursor
    const uint32_t h = head_.load(std::memory_order_acquire);
    if (h - r < 4u) return 0;  // empty (or only a torn header, which cannot happen)
    uint32_t size = 0;
    copyOut(buf_, mask_, r, &size, 4);
    const uint32_t n = (size < bufSize) ? size : bufSize;
    if (n > 0) copyOut(buf_, mask_, r + 4, dst, n);
    tail_.store(r + 4u + size, std::memory_order_release);
    return size;
}

bool Lv2PacketRing::empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// Lv2Worker
// ---------------------------------------------------------------------------

Lv2Worker::Lv2Worker()
    : requests_(16384), responses_(16384) {
    requestBuf_.resize(kMaxPacket);
    responseBuf_.resize(kMaxPacket);
    sched_.handle = this;
    sched_.schedule_work = &Lv2Worker::scheduleTrampoline;
}

Lv2Worker::~Lv2Worker() {
    stop();
}

void Lv2Worker::start(const LV2_Worker_Interface* iface, LV2_Handle handle) {
    if (iface == nullptr || running_.load(std::memory_order_acquire)) return;
    iface_ = iface;
    handle_ = handle;
    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { threadMain(); });
}

void Lv2Worker::stop() {
    if (!running_.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
    // Drop anything still queued; nobody will process it after stop().
    if (!requests_.empty()) {
        LSPA_LOG_WARN("Lv2Worker: discarded pending worker requests on stop");
    }
    // Drain both rings so a later start() begins clean.
    while (requests_.read(requestBuf_.data(), static_cast<uint32_t>(requestBuf_.size())) != 0) {
    }
    while (responses_.read(responseBuf_.data(), static_cast<uint32_t>(responseBuf_.size())) != 0) {
    }
}

LV2_Worker_Status Lv2Worker::scheduleTrampoline(LV2_Worker_Schedule_Handle handle,
                                                uint32_t size, const void* data) {
    return static_cast<Lv2Worker*>(handle)->schedule(size, data);
}

LV2_Worker_Status Lv2Worker::schedule(uint32_t size, const void* data) {
    // Called from the audio thread: no locks, no allocation, no logging.
    if (!running_.load(std::memory_order_acquire) || size > kMaxPacket) {
        ++dropped_;
        return LV2_WORKER_ERR_NO_SPACE;
    }
    if (!requests_.write(size, data)) {
        ++dropped_;  // queue full: drop the request (LV2 allows this)
        return LV2_WORKER_ERR_NO_SPACE;
    }
    // Waking the worker without holding the mutex is allowed by POSIX and
    // does not block; a lost wakeup is bounded by the 5 ms poll below.
    cv_.notify_one();
    return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status Lv2Worker::respondTrampoline(LV2_Worker_Respond_Handle handle,
                                               uint32_t size, const void* data) {
    auto* self = static_cast<Lv2Worker*>(handle);
    if (size == 0 || size > kMaxPacket || !self->responses_.write(size, data)) {
        ++self->dropped_;
        return LV2_WORKER_ERR_NO_SPACE;
    }
    return LV2_WORKER_SUCCESS;
}

void Lv2Worker::threadMain() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_.load(std::memory_order_acquire)) break;
            // Bounded wait: notifications from schedule()/stop() wake us early,
            // the timeout guarantees liveness even if a wakeup is lost.
            cv_.wait_for(lock, std::chrono::milliseconds(5));
            if (stop_.load(std::memory_order_acquire)) break;
        }
        // Drain all pending requests; at most one work() call at a time
        // (LV2 requires that guarantee, and we have a single worker thread).
        for (;;) {
            const uint32_t size = requests_.read(requestBuf_.data(),
                                                 static_cast<uint32_t>(requestBuf_.size()));
            if (size == 0) break;
            if (size > kMaxPacket) continue;  // truncated by ring; nothing to do
            iface_->work(handle_, &Lv2Worker::respondTrampoline, this, size, requestBuf_.data());
        }
    }
}

void Lv2Worker::emitResponses() {
    // RT-safe: responses arrive in a preallocated ring and are copied into a
    // preallocated buffer before the plugin sees them.
    if (!iface_) return;
    for (;;) {
        const uint32_t size = responses_.read(responseBuf_.data(),
                                              static_cast<uint32_t>(responseBuf_.size()));
        if (size == 0) break;
        if (size > kMaxPacket) continue;  // cannot happen: writes are bounded
        iface_->work_response(handle_, size, responseBuf_.data());
    }
}

void Lv2Worker::endRun() {
    if (iface_ && iface_->end_run) iface_->end_run(handle_);
}

}  // namespace lspa
