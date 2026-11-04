#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace vofa::test {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Case {
    std::string name;
    void (*fn)();
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)())
    {
        registry().push_back({name, fn});
    }
};

} // namespace vofa::test

#define VOFA_TEST(name) \
    static void name(); \
    static ::vofa::test::Registrar name##_registrar(#name, name); \
    static void name()

#define VOFA_CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw ::vofa::test::Failure( \
                std::string(__FILE__) + ":" + std::to_string(__LINE__) \
                + " CHECK failed: " #condition); \
        } \
    } while (false)

#define VOFA_CHECK_EQ(actual, expected) \
    do { \
        const auto& vofa_actual = (actual); \
        const auto& vofa_expected = (expected); \
        if (!(vofa_actual == vofa_expected)) { \
            throw ::vofa::test::Failure( \
                std::string(__FILE__) + ":" + std::to_string(__LINE__) \
                + " CHECK_EQ failed: " #actual " != " #expected); \
        } \
    } while (false)
