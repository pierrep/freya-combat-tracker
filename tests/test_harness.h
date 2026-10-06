#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void()> body;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Register {
    Register(std::string name, std::function<void()> body)
    {
        registry().push_back({std::move(name), std::move(body)});
    }
};

struct Failure : std::exception {
    std::string message;
    explicit Failure(std::string m) : message(std::move(m)) {}
    const char* what() const noexcept override { return message.c_str(); }
};

inline int runAll()
{
    int failed = 0;
    for (const Case& c : registry()) {
        try {
            c.body();
            std::cout << "PASS " << c.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "FAIL " << c.name << ": " << e.what() << "\n";
        }
    }
    std::cout << (registry().size() - static_cast<std::size_t>(failed)) << "/" << registry().size()
              << " passed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace test

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                 \
    static void TEST_CONCAT(test_fn_, __LINE__)();                                      \
    static const test::Register TEST_CONCAT(test_reg_, __LINE__)(name, &TEST_CONCAT(test_fn_, __LINE__)); \
    static void TEST_CONCAT(test_fn_, __LINE__)()

#define CHECK(expr)                                                                                  \
    do {                                                                                             \
        if (!(expr)) {                                                                               \
            throw test::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": CHECK(" #expr ")"); \
        }                                                                                            \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                   \
    do {                                                                                             \
        const auto& test_actual_ = (actual);                                                         \
        const auto& test_expected_ = (expected);                                                     \
        if (!(test_actual_ == test_expected_)) {                                                     \
            throw test::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) +            \
                                ": CHECK_EQ(" #actual ", " #expected ")");                           \
        }                                                                                            \
    } while (false)

#define CHECK_THROWS(exception_type, expr)                                                           \
    do {                                                                                             \
        bool test_threw_ = false;                                                                    \
        try {                                                                                        \
            (void)(expr);                                                                            \
        } catch (const exception_type&) {                                                            \
            test_threw_ = true;                                                                      \
        }                                                                                            \
        if (!test_threw_) {                                                                          \
            throw test::Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) +            \
                                ": expected " #exception_type " from " #expr);                       \
        }                                                                                            \
    } while (false)

#define TEST_MAIN()            \
    int main()                 \
    {                          \
        return test::runAll(); \
    }
