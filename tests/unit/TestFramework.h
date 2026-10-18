#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "RuntimeError.h"

// A small test framework, so the tests need nothing beyond the C++ standard
// library. TEST_CASE registers a function under a suite name; the CHECK
// macros report a failure and carry on, so one run shows every failure.

namespace test {

struct Case {
	std::string suite;
	std::string name;
	void (*run)();
};

inline std::vector<Case>& cases() {
	static std::vector<Case> all;
	return all;
}

struct Registration {
	Registration(const char* suite, const char* name, void (*run)()) {
		cases().push_back({ suite, name, run });
	}
};

inline int& failures() {
	static int count = 0;
	return count;
}

inline int& checks() {
	static int count = 0;
	return count;
}

inline void fail(const char* file, int line, const std::string& message) {
	++failures();
	std::cout << "  " << file << ":" << line << ": " << message << "\n";
}

template <class T>
std::string show(const T& value) {
	if constexpr (std::is_same_v<T, std::string>) {
		return "\"" + value + "\"";
	}
	else if constexpr (std::is_same_v<T, bool>) {
		return value ? "true" : "false";
	}
	else if constexpr (requires(std::ostream& out) { out << value; }) {
		std::ostringstream out;
		out.precision(17);
		out << value;
		return out.str();
	}
	else {
		return "(a value that cannot be printed)";
	}
}

}

#define TEST_CASE(suite, name)                                                      \
	static void name();                                                             \
	static const test::Registration name##_registration(suite, #name, name);       \
	static void name()

#define CHECK(condition)                                                            \
	do {                                                                            \
		++test::checks();                                                           \
		if (!(condition)) {                                                         \
			test::fail(__FILE__, __LINE__, "CHECK(" #condition ") failed");         \
		}                                                                           \
	} while (false)

#define CHECK_EQ(actual, expected)                                                  \
	do {                                                                            \
		++test::checks();                                                           \
		const auto& actual_ = (actual);                                             \
		const auto& expected_ = (expected);                                         \
		if (!(actual_ == expected_)) {                                              \
			test::fail(__FILE__, __LINE__, std::string(#actual) + " is "            \
				+ test::show(actual_) + ", expected " + test::show(expected_));    \
		}                                                                           \
	} while (false)

// Checks that the statement throws a RuntimeError with this code.
#define CHECK_ERROR(expectedCode, ...)                                              \
	do {                                                                            \
		++test::checks();                                                           \
		try {                                                                       \
			__VA_ARGS__;                                                            \
			test::fail(__FILE__, __LINE__, std::string(#__VA_ARGS__)                \
				+ " did not throw " + (expectedCode));                              \
		}                                                                           \
		catch (const RuntimeError& error_) {                                        \
			if (error_.code() != std::string(expectedCode)) {                       \
				test::fail(__FILE__, __LINE__, std::string(#__VA_ARGS__) + " threw "\
					+ error_.code() + " (" + error_.what() + "), expected "         \
					+ (expectedCode));                                              \
			}                                                                       \
		}                                                                           \
	} while (false)
