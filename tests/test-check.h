#pragma once

#include <cstdio>
#include <cstdlib>

namespace d2rl::test {

[[noreturn]] inline void Fail(
    const char* expression,
    const char* file,
    int line) noexcept {
    std::fprintf(
        stderr,
        "%s(%d): test requirement failed: %s\n",
        file,
        line,
        expression);
    std::fflush(stderr);
    std::abort();
}

} // namespace d2rl::test

#define TEST_REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            ::d2rl::test::Fail(#condition, __FILE__, __LINE__); \
        } \
    } while (false)
