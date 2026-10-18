#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "RuntimeError.h"
#include "Types.h"
#include "Value.h"

class Files;

// The built-in functions of spec 9.
//
// Arguments follow the implicit conversions of spec 4.3: a CHAR is accepted
// where a STRING is expected and an INTEGER where a REAL is. Positions in
// strings are 1-based, and characters are bytes (spec 13.1, D17).

// Whether `name` is a built-in function. These names are reserved (spec 2.2).
bool isBuiltinFunction(std::string_view name);

// The check of a built-in call against spec 9, used before running with the
// argument types known then (nullptr where not known), and again when it runs.
struct BuiltinCheck {
	const Type* result = nullptr; // the call's type; nullptr if known only when it runs
	std::string error;            // empty if the call is allowed
	std::string code;             // M003 for the wrong number of arguments, M004 for a wrong type
	size_t argument = 0;          // for M004: which argument is wrong

	bool ok() const { return error.empty(); }
};
BuiltinCheck checkBuiltinCall(std::string_view name, const std::vector<const Type*>& argumentTypes);

// An argument of the wrong type (R002). `index` says which argument, so the
// error can point at it.
class ArgumentError : public RuntimeError {
public:
	ArgumentError(size_t index, const std::string& message) : RuntimeError("R002", message), index_(index) {}

	size_t index() const { return index_; }

private:
	size_t index_;
};

class Builtins {
public:
	// `seed` makes RAND repeatable (--seed); without one RAND is seeded
	// randomly. EOF asks `files`, which must outlive this object.
	Builtins(std::optional<std::uint64_t> seed, Files& files);

	// Calls a built-in function with its arguments, already evaluated left
	// to right. Throws M003 for the wrong number of arguments, ArgumentError
	// for an argument of the wrong type, and R006 for an argument value the
	// function cannot take (R004 and R005 where those fit better).
	Value call(std::string_view name, std::vector<Value> arguments);

private:
	// A REAL in [0, limit), from a generator whose output is fixed by the
	// C++ standard, so a seed gives the same numbers on every platform.
	double random(std::int64_t limit);

	std::mt19937_64 generator_;
	Files& files_;
};
