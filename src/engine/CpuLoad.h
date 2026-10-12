// CpuLoad.h - audio callback load estimate (busy time / period time).

#pragma once

#include <atomic>

namespace lspa {

class CpuLoad {
public:
    void push(double busySeconds, double periodSeconds) {
        if (periodSeconds <= 0.0) return;
        const double v = busySeconds / periodSeconds;
        const double prev = value_.load(std::memory_order_relaxed);
        const double smoothed = prev + 0.08 * (v - prev);
        value_.store(static_cast<float>(smoothed), std::memory_order_relaxed);
    }

    float value() const { return value_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> value_{0.0f};
};

}  // namespace lspa
