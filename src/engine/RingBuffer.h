// RingBuffer.h - single-producer/single-consumer interleaved float ring.
// Real-time safe on both sides: no locks, no allocation after construction.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lspa {

class FloatRing {
public:
    explicit FloatRing(size_t capacitySamples)
        : buf_(capacitySamples + kGuard, 0.0f), mask_(capacitySamples + kGuard - 1) {
        // capacity must be a power of two (plus guard slot)
    }

    size_t capacity() const { return mask_; }

    size_t writeSpace() const {
        const uint64_t w = write_.load(std::memory_order_relaxed);
        const uint64_t r = read_.load(std::memory_order_acquire);
        return mask_ - static_cast<size_t>(w - r);
    }

    size_t readAvailable() const {
        const uint64_t w = write_.load(std::memory_order_acquire);
        const uint64_t r = read_.load(std::memory_order_relaxed);
        return static_cast<size_t>(w - r);
    }

    // Producer: append interleaved samples. Returns samples written.
    size_t write(const float* data, size_t n) {
        const uint64_t w = write_.load(std::memory_order_relaxed);
        const uint64_t r = read_.load(std::memory_order_acquire);
        size_t space = mask_ - static_cast<size_t>(w - r);
        if (n > space) n = space;
        const size_t idx = static_cast<size_t>(w) & mask_;
        const size_t first = std::min(n, mask_ + 1 - idx);
        for (size_t i = 0; i < first; ++i) buf_[idx + i] = data[i];
        for (size_t i = first; i < n; ++i) buf_[i - first] = data[i];
        write_.store(w + n, std::memory_order_release);
        return n;
    }

    // Consumer: pop interleaved samples. Returns samples read.
    size_t read(float* out, size_t n) {
        n = applyFlush(n);
        const uint64_t r = read_.load(std::memory_order_relaxed);
        const uint64_t w = write_.load(std::memory_order_acquire);
        size_t avail = static_cast<size_t>(w - r);
        if (n > avail) n = avail;
        const size_t idx = static_cast<size_t>(r) & mask_;
        const size_t first = std::min(n, mask_ + 1 - idx);
        for (size_t i = 0; i < first; ++i) out[i] = buf_[idx + i];
        for (size_t i = first; i < n; ++i) out[i - first] = buf_[i];
        read_.store(r + n, std::memory_order_release);
        return n;
    }

    // Consumer: discard everything currently buffered.
    void flush() {
        const uint64_t w = write_.load(std::memory_order_acquire);
        read_.store(w, std::memory_order_release);
    }

    // Producer: ask consumer to drop everything buffered (seek, track change).
    void producerFlush() {
        const uint64_t w = write_.load(std::memory_order_relaxed);
        flushMark_.store(w, std::memory_order_release);
    }

private:
    static constexpr size_t kGuard = 1;  // keeps power-of-two mask + full slot

    size_t applyFlush(size_t n) {
        const uint64_t f = flushMark_.load(std::memory_order_acquire);
        const uint64_t r = read_.load(std::memory_order_relaxed);
        if (f > r) {
            read_.store(f, std::memory_order_release);
        }
        return n;
    }

    std::vector<float> buf_;
    size_t mask_;
    std::atomic<uint64_t> write_{0};
    std::atomic<uint64_t> read_{0};
    std::atomic<uint64_t> flushMark_{0};
};

// Round up to a power of two >= v.
inline size_t nextPow2(size_t v) {
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

}  // namespace lspa
