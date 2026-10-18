#include <exception>
#include <iostream>
#include <string>

#include "TestFramework.h"

// Usage: unit_tests [suite]
// Runs every test case, or only those in one suite.
int main(int argc, char* argv[]) {
	std::string suite = argc > 1 ? argv[1] : "";
	int run = 0;
	int failed = 0;

	for (const test::Case& testCase : test::cases()) {
		if (!suite.empty() && testCase.suite != suite) {
			continue;
		}
		int failuresBefore = test::failures();
		try {
			testCase.run();
		}
		catch (const RuntimeError& error) {
			test::fail(testCase.name.c_str(), 0, "unexpected RuntimeError " + error.code() + ": " + error.what());
		}
		catch (const std::exception& error) {
			test::fail(testCase.name.c_str(), 0, std::string("unexpected exception: ") + error.what());
		}
		bool passed = test::failures() == failuresBefore;
		std::cout << (passed ? "PASS " : "FAIL ") << testCase.suite << "." << testCase.name << "\n";
		++run;
		if (!passed) {
			++failed;
		}
	}

	if (run == 0) {
		std::cout << "No test cases in suite \"" << suite << "\"\n";
		return 1;
	}
	std::cout << run << " test cases, " << test::checks() << " checks, " << failed << " failed\n";
	return failed == 0 ? 0 : 1;
}
