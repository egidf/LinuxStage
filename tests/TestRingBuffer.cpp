#include "TestUtil.h"

#include <vector>

#include "engine/RingBuffer.h"

using namespace lspa;

TEST_CASE(RingBufferBasicReadWrite) {
    FloatRing ring(nextPow2(16) - 1);
    std::vector<float> in = {1, 2, 3, 4, 5};
    CHECK(ring.write(in.data(), in.size()) == in.size());
    CHECK(ring.readAvailable() == 5);

    std::vector<float> out(5, 0.0f);
    CHECK(ring.read(out.data(), 5) == 5);
    for (int i = 0; i < 5; ++i) CHECK(out[static_cast<size_t>(i)] == static_cast<float>(i + 1));
    CHECK(ring.readAvailable() == 0);
}

TEST_CASE(RingBufferCapacityLimit) {
    // FloatRing holds capacitySamples usable samples where the internal mask
    // is nextPow2(n) - 1 (a power-of-two ring keeps indices cheap).
    FloatRing ring(nextPow2(8) - 1);  // 7 usable slots
    const size_t capacity = ring.capacity();
    CHECK(capacity == 7);
    std::vector<float> in(20, 1.0f);
    const size_t written = ring.write(in.data(), in.size());
    CHECK(written == capacity);
    CHECK(ring.writeSpace() == 0);
    // Writing more must be refused without corrupting the stream.
    CHECK(ring.write(in.data(), 4) == 0);
    std::vector<float> out(20, 0.0f);
    CHECK(ring.read(out.data(), 20) == capacity);
    for (size_t i = 0; i < capacity; ++i) CHECK(out[i] == 1.0f);
}

TEST_CASE(RingBufferWrapAround) {
    FloatRing ring(nextPow2(32) - 1);
    std::vector<float> chunk(10, 0.5f);
    std::vector<float> out(10, 0.0f);
    // Push the indices around the buffer many times.
    for (int i = 0; i < 50; ++i) {
        CHECK(ring.write(chunk.data(), chunk.size()) == chunk.size());
        CHECK(ring.read(out.data(), out.size()) == out.size());
        for (float v : out) CHECK(v == 0.5f);
    }
}

TEST_CASE(RingBufferProducerFlush) {
    FloatRing ring(nextPow2(64) - 1);
    std::vector<float> in(20, 2.0f);
    ring.write(in.data(), in.size());
    CHECK(ring.readAvailable() == 20);
    ring.producerFlush();  // seek / track change: consumer must drop the data
    std::vector<float> out(20, 0.0f);
    CHECK(ring.read(out.data(), 20) == 0);
    // New data after the flush must come through.
    std::vector<float> fresh(4, 7.0f);
    CHECK(ring.write(fresh.data(), fresh.size()) == fresh.size());
    CHECK(ring.read(out.data(), 20) == 4);
    CHECK(out[0] == 7.0f);
}

TEST_CASE(RingBufferConsumerFlush) {
    FloatRing ring(nextPow2(64) - 1);
    std::vector<float> in(10, 3.0f);
    ring.write(in.data(), in.size());
    ring.flush();
    CHECK(ring.readAvailable() == 0);
}

TEST_CASE(NextPow2) {
    CHECK(nextPow2(1) == 1);
    CHECK(nextPow2(2) == 2);
    CHECK(nextPow2(3) == 4);
    CHECK(nextPow2(1000) == 1024);
}
