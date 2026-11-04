#include "test_framework.h"

#include <exception>
#include <iostream>

int main()
{
    int failed = 0;
    int passed = 0;

    for (const auto& test : vofa::test::registry()) {
        try {
            test.fn();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
