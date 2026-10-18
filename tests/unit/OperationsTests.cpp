#include <limits>

#include "Helpers.h"
#include "Operations.h"
#include "TestFramework.h"

// Operators on values (spec 4.4, 6).

namespace {

const std::int64_t MaxInt = std::numeric_limits<std::int64_t>::max();
const std::int64_t MinInt = std::numeric_limits<std::int64_t>::min();

}

TEST_CASE("operations", integer_and_real_arithmetic) {
	using enum BinaryOp;
	CHECK_EQ(describe(applyBinary(Add, I(2), I(3))), "INTEGER:5");
	CHECK_EQ(describe(applyBinary(Add, I(2), R(0.5))), "REAL:2.5");
	CHECK_EQ(describe(applyBinary(Multiply, R(2.5), I(2))), "REAL:5.0");
	CHECK_EQ(describe(applyBinary(Subtract, I(MinInt + 1), I(1))), "INTEGER:" + std::to_string(MinInt));
	CHECK_EQ(describe(applyUnary(UnaryOp::Negate, I(5))), "INTEGER:-5");
	CHECK_EQ(describe(applyUnary(UnaryOp::Plus, R(1.5))), "REAL:1.5");
}

TEST_CASE("operations", division_is_always_real) {
	using enum BinaryOp;
	CHECK_EQ(describe(applyBinary(Divide, I(6), I(3))), "REAL:2.0");
	CHECK_EQ(describe(applyBinary(Divide, I(1), I(10))), "REAL:0.1");
	CHECK_EQ(describe(applyBinary(Divide, I(1), I(3))), "REAL:0.3333333333333333");
}

TEST_CASE("operations", div_and_mod_truncate_toward_zero) {
	using enum BinaryOp;
	CHECK_EQ(describe(applyBinary(IntDiv, I(-7), I(2))), "INTEGER:-3");
	CHECK_EQ(describe(applyBinary(Mod, I(-7), I(2))), "INTEGER:-1");
	CHECK_EQ(describe(applyBinary(Mod, I(7), I(-2))), "INTEGER:1");
	CHECK_EQ(describe(applyBinary(IntDiv, I(7), I(2))), "INTEGER:3");
	CHECK_EQ(describe(applyBinary(Mod, I(MinInt), I(-1))), "INTEGER:0");
	// A = B * (A DIV B) + (A MOD B)
	for (std::int64_t a : { -7, -1, 0, 5, 13 }) {
		for (std::int64_t b : { -3, -1, 2, 5 }) {
			std::int64_t q = std::get<std::int64_t>(applyBinary(IntDiv, I(a), I(b)));
			std::int64_t r = std::get<std::int64_t>(applyBinary(Mod, I(a), I(b)));
			CHECK_EQ(b * q + r, a);
		}
	}
}

TEST_CASE("operations", integer_overflow_is_an_error) {
	using enum BinaryOp;
	CHECK_ERROR("R005", applyBinary(Add, I(MaxInt), I(1)));
	CHECK_ERROR("R005", applyBinary(Subtract, I(MinInt), I(1)));
	CHECK_ERROR("R005", applyBinary(Multiply, I(MaxInt / 2 + 1), I(2)));
	CHECK_ERROR("R005", applyBinary(Multiply, I(MinInt), I(-1)));
	CHECK_ERROR("R005", applyUnary(UnaryOp::Negate, I(MinInt)));
	CHECK_ERROR("R005", applyBinary(IntDiv, I(MinInt), I(-1)));
	CHECK_ERROR("R005", applyBinary(Multiply, R(1e308), R(10)));
}

TEST_CASE("operations", division_by_zero_is_an_error) {
	using enum BinaryOp;
	CHECK_ERROR("R004", applyBinary(Divide, I(1), I(0)));
	CHECK_ERROR("R004", applyBinary(Divide, R(1.0), R(0.0)));
	CHECK_ERROR("R004", applyBinary(IntDiv, I(1), I(0)));
	CHECK_ERROR("R004", applyBinary(Mod, I(1), I(0)));
}

TEST_CASE("operations", operand_type_errors) {
	using enum BinaryOp;
	CHECK_ERROR("R002", applyBinary(IntDiv, R(7.0), I(2)));
	CHECK_ERROR("R002", applyBinary(Add, S("a"), I(1)));
	CHECK_ERROR("R002", applyBinary(Add, S("a"), S("b")));
	CHECK_ERROR("R002", applyBinary(Multiply, B(true), I(1)));
	CHECK_ERROR("R002", applyBinary(Concat, S("n="), I(5)));
	CHECK_ERROR("R002", applyUnary(UnaryOp::Not, I(1)));
	CHECK_ERROR("R002", applyBinary(Or, I(1), B(true)));
	CHECK_ERROR("R001", applyBinary(Add, Value{}, I(1)));
}

TEST_CASE("operations", concatenation) {
	CHECK_EQ(describe(applyBinary(BinaryOp::Concat, S("Total: "), C('X'))), "STRING:Total: X");
	CHECK_EQ(describe(applyBinary(BinaryOp::Concat, C('a'), C('b'))), "STRING:ab");
}

TEST_CASE("operations", comparisons) {
	using enum BinaryOp;
	CHECK_EQ(describe(applyBinary(Equal, I(5), R(5.0))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(Less, I(2), R(2.5))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(Equal, C('A'), S("A"))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(Less, S("Apple"), S("apple"))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(Less, S("ab"), S("abc"))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(GreaterEqual, S("b"), C('a'))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(NotEqual, B(true), B(false))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(Less, Value{ Date{ 2005, 1, 2 } }, Value{ Date{ 2005, 2, 1 } })), "BOOLEAN:TRUE");
	CHECK_ERROR("R002", applyBinary(Less, B(true), B(false)));
	CHECK_ERROR("R002", applyBinary(Equal, I(1), S("1")));
}

TEST_CASE("operations", booleans) {
	CHECK_EQ(describe(applyBinary(BinaryOp::And, B(true), B(false))), "BOOLEAN:FALSE");
	CHECK_EQ(describe(applyBinary(BinaryOp::Or, B(false), B(true))), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyUnary(UnaryOp::Not, B(false))), "BOOLEAN:TRUE");
}

TEST_CASE("operations", enumerated_values) {
	auto compiled = compile("TYPE Season = (Spring, Summer, Autumn, Winter)\n");
	CHECK(compiled->clean);
	const Type* season = compiled->types.find("season");
	Value spring = EnumValue{ season, 0 };
	Value autumn = EnumValue{ season, 2 };
	CHECK_EQ(describe(applyBinary(BinaryOp::Add, autumn, I(1))), "Season:Winter");
	CHECK_EQ(describe(applyBinary(BinaryOp::Add, I(1), spring)), "Season:Summer");
	CHECK_EQ(describe(applyBinary(BinaryOp::Subtract, autumn, I(2))), "Season:Spring");
	CHECK_ERROR("R009", applyBinary(BinaryOp::Add, autumn, I(2)));
	CHECK_ERROR("R009", applyBinary(BinaryOp::Subtract, autumn, I(3)));
	CHECK_ERROR("R009", applyBinary(BinaryOp::Add, autumn, I(MaxInt)));
	CHECK_EQ(describe(applyBinary(BinaryOp::Less, spring, autumn)), "BOOLEAN:TRUE");
	CHECK_ERROR("R002", applyBinary(BinaryOp::Add, autumn, R(1.0)));
}

TEST_CASE("operations", sets) {
	auto compiled = compile("TYPE Vowel = SET OF CHAR\n");
	const Type* vowelType = compiled->types.find("Vowel");
	Value vowels = SetRef(std::make_shared<const SetValue>(SetValue{ vowelType, { C('A'), C('E') } }));
	CHECK_EQ(describe(applyBinary(BinaryOp::In, C('E'), vowels)), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(BinaryOp::In, S("B"), vowels)), "BOOLEAN:FALSE");
	CHECK_ERROR("R002", applyBinary(BinaryOp::In, I(1), vowels));
	CHECK_ERROR("R002", applyBinary(BinaryOp::Equal, vowels, vowels));
}

TEST_CASE("operations", objects_and_pointers) {
	auto compiled = compile("CLASS Pet\n   PUBLIC Name : STRING\nENDCLASS\n");
	const Type* pet = compiled->types.find("Pet");
	BoundEvaluator bound = literalBound;
	Value kitty = makeObject(pet, bound, false);
	CHECK_ERROR("R002", applyBinary(BinaryOp::Equal, kitty, kitty));
	CHECK_ERROR("R008", applyBinary(BinaryOp::Equal, ObjectRef{}, I(1)));

	CellPtr cell = std::make_shared<Cell>(Cell{ integerType(), I(1) });
	Value p1 = PointerValue{ compiled->types.pointerTo(integerType()), cell };
	Value p2 = PointerValue{ compiled->types.pointerTo(integerType()), cell };
	Value null = PointerValue{ compiled->types.pointerTo(integerType()), {} };
	CHECK_EQ(describe(applyBinary(BinaryOp::Equal, p1, p2)), "BOOLEAN:TRUE");
	CHECK_EQ(describe(applyBinary(BinaryOp::Equal, p1, null)), "BOOLEAN:FALSE");
	CHECK_ERROR("R002", applyBinary(BinaryOp::Less, p1, p2));
}

TEST_CASE("operations", require_helpers) {
	CHECK(requireBoolean(B(true), "The condition"));
	CHECK_ERROR("R002", requireBoolean(I(1), "The IF condition"));
	CHECK_EQ(requireInteger(I(7), "The index"), std::int64_t{ 7 });
	CHECK_ERROR("R002", requireInteger(R(7.0), "The index"));
	CHECK_ERROR("R001", requireInteger(Value{}, "The index"));
}
