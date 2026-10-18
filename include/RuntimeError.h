#pragma once

#include <stdexcept>
#include <string>

// A runtime error (spec 12.2, codes R001-R014). Thrown by the value and
// operator code, which knows what went wrong but not where; the interpreter
// catches it and adds the source location and call chain.
class RuntimeError : public std::runtime_error {
public:
	RuntimeError(std::string code, const std::string& message)
		: std::runtime_error(message), code_(std::move(code)) {}

	const std::string& code() const { return code_; }

private:
	std::string code_;
};
