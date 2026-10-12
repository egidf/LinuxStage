// LilvLock.h - process-wide serialization of every Lilv call.
//
// LilvWorld (and the sord/serd store behind it) is not thread-safe, and
// bundle data is parsed lazily on first access - so discovery (scan),
// instantiation, teardown and editor support must serialize even though they
// are driven by different threads (catalog scan worker, engine, GUI).
//
// The mutex is recursive: an outer holder (scan) routinely calls code that
// takes the lock again (pickUiForX11, a failed instantiate tearing itself
// down).
//
// Scope: this lock guards LILV calls only. Never hold it on the audio
// thread; process()/setParameter() never take it.
#pragma once

#include <mutex>

namespace lspa {

class LilvLock {
public:
    LilvLock() { mutex().lock(); }
    ~LilvLock() { mutex().unlock(); }

    LilvLock(const LilvLock&) = delete;
    LilvLock& operator=(const LilvLock&) = delete;

    // Single shared recursive mutex (inline function-local static: exactly
    // one instance across all translation units).
    static std::recursive_mutex& mutex() {
        static std::recursive_mutex m;
        return m;
    }
};

}  // namespace lspa
