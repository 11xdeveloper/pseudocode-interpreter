#include <filesystem>

#include "Builtins.h"
#include "Files.h"
#include "Helpers.h"
#include "TestFramework.h"

// The built-in functions (spec 9).

namespace {

// Built-ins with a fixed seed and no open files.
struct Calls {
	Files files{ std::filesystem::temp_directory_path() };
	Builtins builtins{ std::uint64_t{ 42 }, files };

	std::string operator()(const char* name, std::vector<Value> arguments) {
		return describe(builtins.call(name, std::move(arguments)));
	}
};

}

TEST_CASE("builtins", strings) {
	Calls call;
	CHECK_EQ(call("LENGTH", { S("Hello") }), "INTEGER:5");
	CHECK_EQ(call("LENGTH", { C('x') }), "INTEGER:1");
	CHECK_EQ(call("RIGHT", { S("ABCDEFGH"), I(3) }), "STRING:FGH");
	CHECK_EQ(call("MID", { S("ABCDEFGH"), I(2), I(3) }), "STRING:BCD");
	CHECK_EQ(call("LEFT", { S("Hello"), I(0) }), "STRING:");
	CHECK_EQ(call("MID", { S("Hello"), I(6), I(0) }), "STRING:");
	CHECK_EQ(call("LCASE", { C('A') }), "CHAR:a");
	CHECK_EQ(call("UCASE", { C('7') }), "CHAR:7");
	CHECK_EQ(call("TO_UPPER", { S("MiXeD 1") }), "STRING:MIXED 1");
	CHECK_EQ(call("TO_LOWER", { C('Q') }), "CHAR:q");
	CHECK_EQ(call("NUM_TO_STR", { R(5.0) }), "STRING:5.0");
	CHECK_EQ(call("STR_TO_NUM", { S("-3.5") }), "REAL:-3.5");
	CHECK_EQ(call("STR_TO_NUM", { C('7') }), "INTEGER:7");
	CHECK_EQ(call("IS_NUM", { S("+3") }), "BOOLEAN:TRUE");
	CHECK_EQ(call("IS_NUM", { S("1e5") }), "BOOLEAN:FALSE");
	CHECK_EQ(call("ASC", { C('A') }), "INTEGER:65");
	CHECK_EQ(call("CHR", { I(66) }), "CHAR:B");
}

TEST_CASE("builtins", string_errors) {
	Calls call;
	CHECK_ERROR("R006", call("LEFT", { S("abc"), I(4) }));
	CHECK_ERROR("R006", call("RIGHT", { S("abc"), I(-1) }));
	CHECK_ERROR("R006", call("MID", { S("Hello"), I(0), I(1) }));
	CHECK_ERROR("R006", call("MID", { S("Hello"), I(4), I(3) }));
	CHECK_ERROR("R006", call("MID", { S("Hello"), I(1), I(-1) }));
	CHECK_ERROR("R006", call("CHR", { I(200) }));
	CHECK_ERROR("R006", call("STR_TO_NUM", { S("12 apples") }));
	CHECK_ERROR("R005", call("STR_TO_NUM", { S("99999999999999999999") }));
	CHECK_ERROR("M003", call("LEFT", { S("abc") }));
	CHECK_ERROR("R002", call("LENGTH", { I(42) }));
	CHECK_ERROR("R002", call("LCASE", { S("ABC") }));
	CHECK_ERROR("R002", call("MID", { S("abc"), R(1.5), I(1) }));
}

TEST_CASE("builtins", wrong_type_points_at_the_argument) {
	Calls call;
	try {
		call("MID", { S("abc"), I(1), S("x") });
		CHECK(false);
	}
	catch (const ArgumentError& error) {
		CHECK_EQ(error.index(), size_t{ 2 });
		CHECK_EQ(error.code(), "R002");
	}
}

TEST_CASE("builtins", numbers) {
	Calls call;
	CHECK_EQ(call("INT", { R(27.5415) }), "INTEGER:27");
	CHECK_EQ(call("INT", { R(-2.7) }), "INTEGER:-2");
	CHECK_EQ(call("INT", { I(9223372036854775807) }), "INTEGER:9223372036854775807");
	CHECK_ERROR("R005", call("INT", { R(1e20) }));
	CHECK_EQ(call("MOD", { I(-7), I(2) }), "INTEGER:-1");
	CHECK_EQ(call("DIV", { I(-7), I(2) }), "INTEGER:-3");
	CHECK_ERROR("R004", call("MOD", { I(5), I(0) }));
	CHECK_ERROR("R006", call("RAND", { I(0) }));
}

TEST_CASE("builtins", round_halves_away_from_zero) {
	Calls call;
	CHECK_EQ(call("ROUND", { R(2.675), I(2) }), "REAL:2.68");
	CHECK_EQ(call("ROUND", { R(1.005), I(2) }), "REAL:1.01");
	CHECK_EQ(call("ROUND", { R(-2.5), I(0) }), "REAL:-3.0");
	CHECK_EQ(call("ROUND", { R(2.5), I(0) }), "REAL:3.0");
	CHECK_EQ(call("ROUND", { R(9.96), I(1) }), "REAL:10.0");
	CHECK_EQ(call("ROUND", { R(-9.96), I(1) }), "REAL:-10.0");
	CHECK_EQ(call("ROUND", { R(-0.004), I(2) }), "REAL:0.0");
	CHECK_EQ(call("ROUND", { R(3.14159), I(10) }), "REAL:3.14159");
	CHECK_EQ(call("ROUND", { I(7), I(1) }), "REAL:7.0");
	CHECK_ERROR("R006", call("ROUND", { R(1.5), I(-1) }));
}

TEST_CASE("builtins", rand_is_repeatable_with_a_seed) {
	Calls first;
	Calls second;
	for (int i = 0; i < 20; ++i) {
		double a = std::get<double>(first.builtins.call("RAND", { I(10) }));
		double b = std::get<double>(second.builtins.call("RAND", { I(10) }));
		CHECK(a == b);
		CHECK(a >= 0.0 && a < 10.0);
	}
	// The same numbers on every platform (spec 9.2).
	Calls fresh;
	CHECK_EQ(fresh("RAND", { I(100) }), "REAL:75.5155532954539");
}

TEST_CASE("builtins", dates) {
	Calls call;
	Value date = Date{ 2005, 1, 2 };
	CHECK_EQ(call("DAY", { date }), "INTEGER:2");
	CHECK_EQ(call("MONTH", { date }), "INTEGER:1");
	CHECK_EQ(call("YEAR", { date }), "INTEGER:2005");
	CHECK_EQ(call("DAYINDEX", { Value{ Date{ 2026, 10, 18 } } }), "INTEGER:1"); // a Sunday
	CHECK_EQ(call("DAYINDEX", { Value{ Date{ 2026, 10, 24 } } }), "INTEGER:7"); // a Saturday
	CHECK_EQ(call("DAYINDEX", { Value{ Date{ 1970, 1, 1 } } }), "INTEGER:5");   // a Thursday
	CHECK_EQ(call("DAYINDEX", { Value{ Date{ 1, 1, 1 } } }), "INTEGER:2");      // a Monday
	CHECK_EQ(call("SETDATE", { I(29), I(2), I(2024) }), "DATE:29/02/2024");
	CHECK_ERROR("R006", call("SETDATE", { I(31), I(2), I(2023) }));
	CHECK_ERROR("R006", call("SETDATE", { I(1), I(13), I(2023) }));
	CHECK(std::holds_alternative<Date>(Calls{}.builtins.call("NOW", {})));
}

TEST_CASE("builtins", eof_needs_an_open_file) {
	Calls call;
	CHECK_ERROR("R010", call("EOF", { S("not-open.txt") }));
}

TEST_CASE("builtins", static_checks) {
	auto ok = [](const char* name, std::vector<const Type*> types) { return checkBuiltinCall(name, types); };
	CHECK(ok("LENGTH", { stringType() }).result == integerType());
	CHECK(ok("LENGTH", { charType() }).ok());
	CHECK(ok("TO_UPPER", { charType() }).result == charType());
	CHECK(ok("TO_UPPER", { stringType() }).result == stringType());
	CHECK(ok("STR_TO_NUM", { stringType() }).result == nullptr);
	CHECK(ok("INT", { integerType() }).ok());
	CHECK(ok("MID", { stringType(), nullptr, integerType() }).ok()); // unknown passes
	BuiltinCheck count = ok("LEFT", { stringType() });
	CHECK_EQ(count.code, "M003");
	BuiltinCheck type = ok("MID", { stringType(), realType(), integerType() });
	CHECK_EQ(type.code, "M004");
	CHECK_EQ(type.argument, size_t{ 1 });
	CHECK(isBuiltinFunction("num_to_str"));
	CHECK(!isBuiltinFunction("PRINT"));
}
