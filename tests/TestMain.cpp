#include "TestUtil.h"

#include <exception>

#include "core/Logger.h"

int main() {
    lspa::Logger::instance().setLevel(lspa::LogLevel::Warning);
    int failed = 0;
    int passed = 0;
    for (const auto& c : lspa::test::registry()) {
        try {
            c.fn();
            std::printf("[ OK ] %s\n", c.name);
            ++passed;
        } catch (const lspa::test::Failure& f) {
            std::printf("[FAIL] %s\n       %s\n", c.name, f.message.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("[FAIL] %s\n       exception: %s\n", c.name, e.what());
            ++failed;
        } catch (...) {
            std::printf("[FAIL] %s\n       unknown exception\n", c.name);
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
