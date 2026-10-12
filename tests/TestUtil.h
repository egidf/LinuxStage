// TestUtil.h - tiny assertion helpers for the lspa test binary.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace lspa::test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct Failure {
    std::string message;
};

}  // namespace lspa::test

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    static ::lspa::test::Registrar reg_##name(#name, name);                    \
    static void name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            char buf_[512];                                                    \
            std::snprintf(buf_, sizeof(buf_), "%s:%d: CHECK failed: %s",       \
                          __FILE__, __LINE__, #cond);                          \
            throw ::lspa::test::Failure{buf_};                                 \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do {                                                                       \
        const double a_ = (a), b_ = (b);                                       \
        if (!(std::fabs(a_ - b_) <= (eps))) {                                  \
            char buf_[512];                                                    \
            std::snprintf(buf_, sizeof(buf_),                                  \
                          "%s:%d: CHECK_NEAR failed: %s=%g vs %s=%g",          \
                          __FILE__, __LINE__, #a, a_, #b, b_);                 \
            throw ::lspa::test::Failure{buf_};                                 \
        }                                                                      \
    } while (0)
