// Lv2Worker.h - worker:schedule support for LV2 plugins (http://lv2plug.in/
// ns/ext/worker). Used from two threads:
//
//   * Real-time side (audio thread): schedule() is called by the plugin from
//     run(); emitResponses()/endRun() are called after run() each cycle.
//     Both paths are lock-free and allocation-free (LV2 worker threading
//     class: "realtime").
//   * Worker thread: runs the plugin's LV2_Worker_Interface->work() method in
//     the background and forwards respond() callbacks into the response ring.
//
// Lifecycle: start() is called from Lv2Instance::activate(),
// stop() from Lv2Instance::deactivate() (joined before returning).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <lv2/worker/worker.h>

namespace lspa {

// Fixed-capacity lock-free single-producer/single-consumer packet ring.
// Storage is preallocated in the constructor; write()/read() never allocate,
// never lock and never log, so they are safe on the audio thread.
class Lv2PacketRing {
public:
    // capacity must be a power of two and >= 4 * maxPacket.
    explicit Lv2PacketRing(uint32_t capacity = 16384);

    Lv2PacketRing(const Lv2PacketRing&) = delete;
    Lv2PacketRing& operator=(const Lv2PacketRing&) = delete;

    // RT-safe. Writes [uint32 size][payload]; returns false when the packet
    // does not fit (caller decides how to report a full queue).
    bool write(uint32_t size, const void* data);

    // RT-safe. Reads one packet into buf, returning its payload size, or 0
    // when the ring is empty. If the packet is larger than bufSize it is
    // truncated (the whole packet is still consumed).
    uint32_t read(void* buf, uint32_t bufSize);

    bool empty() const;

private:
    uint32_t capacity_;
    uint32_t mask_;
    std::vector<uint8_t> buf_;
    alignas(64) std::atomic<uint32_t> head_{0};  // producer cursor (free-running, bytes)
    alignas(64) std::atomic<uint32_t> tail_{0};  // consumer cursor (free-running, bytes)
};

// worker:schedule host implementation for one LV2 instance.
class Lv2Worker {
public:
    // Largest request/response payload accepted (larger packets are dropped
    // with LV2_WORKER_ERR_NO_SPACE).
    static constexpr uint32_t kMaxPacket = 4096;

    Lv2Worker();
    ~Lv2Worker();

    Lv2Worker(const Lv2Worker&) = delete;
    Lv2Worker& operator=(const Lv2Worker&) = delete;

    // Feature data to expose as worker:schedule. Stable address.
    LV2_Worker_Schedule* scheduleFeature() { return &sched_; }

    // Start/stop the background thread. start() is idempotent.
    void start(const LV2_Worker_Interface* iface, LV2_Handle handle);
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // RT-safe: deliver queued worker responses via work_response(). Called
    // after the plugin's run() each cycle (same order as the reference host
    // jalv).
    void emitResponses();

    // RT-safe: must be called after every process cycle (LV2_Worker_Interface
    //::end_run contract).
    void endRun();

    // Diagnostics (non-RT): packets dropped because a queue was full.
    uint32_t droppedPackets() const { return dropped_.load(std::memory_order_relaxed); }

private:
    static LV2_Worker_Status scheduleTrampoline(LV2_Worker_Schedule_Handle handle,
                                                uint32_t size, const void* data);
    static LV2_Worker_Status respondTrampoline(LV2_Worker_Respond_Handle handle,
                                               uint32_t size, const void* data);
    LV2_Worker_Status schedule(uint32_t size, const void* data);  // RT-safe
    void threadMain();

    Lv2PacketRing requests_;                              // RT -> worker
    Lv2PacketRing responses_;                             // worker -> RT
    std::vector<uint8_t> requestBuf_;                     // worker thread only
    std::vector<uint8_t> responseBuf_;                    // RT thread only

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<uint32_t> dropped_{0};

    const LV2_Worker_Interface* iface_ = nullptr;  // set before start(), then immutable
    LV2_Handle handle_ = nullptr;
    LV2_Worker_Schedule sched_{};
};

}  // namespace lspa
