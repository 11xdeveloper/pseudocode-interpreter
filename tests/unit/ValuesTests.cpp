#include "Helpers.h"
#include "Operations.h"
#include "TestFramework.h"

// Values, cells, copying, formatting and text conversion (spec 4, 5, 7.2).

TEST_CASE("values", real_output_format) {
	CHECK_EQ(formatReal(5.0), "5.0");
	CHECK_EQ(formatReal(0.1), "0.1");
	CHECK_EQ(formatReal(3.14159), "3.14159");
	CHECK_EQ(formatReal(-2.5), "-2.5");
	CHECK_EQ(formatReal(0.1 + 0.2), "0.30000000000000004");
	CHECK_EQ(formatReal(1e20), "1.0e+20");
	CHECK_EQ(formatReal(1e-7), "1.0e-07");
	CHECK_EQ(formatReal(1.5e-7), "1.5e-07");
}

TEST_CASE("values", other_output_formats) {
	CHECK_EQ(formatDate(Date{ 2005, 1, 2 }), "02/01/2005");
	CHECK_EQ(formatValue(B(true)), "TRUE");
	CHECK_EQ(formatValue(I(-42)), "-42");
	CHECK_EQ(formatValue(C('x')), "x");
	CHECK_EQ(formatValue(S("")), "");
	CHECK_ERROR("R001", formatValue(Value{}));
}

TEST_CASE("values", number_text) {
	CHECK(isNumberText("42"));
	CHECK(isNumberText("-3.25"));
	CHECK(isNumberText("+7"));
	CHECK(!isNumberText(" 3"));
	CHECK(!isNumberText(".5"));
	CHECK(!isNumberText("5."));
	CHECK(!isNumberText(""));
	CHECK(!isNumberText("-"));
	CHECK(!isNumberText("1e5"));
	CHECK_EQ(describe(*parseNumber("12")), "INTEGER:12");
	CHECK_EQ(describe(*parseNumber("-0.5")), "REAL:-0.5");
	CHECK(!parseNumber("abc"));
	CHECK_ERROR("R005", parseNumber("99999999999999999999"));
}

TEST_CASE("values", dates_from_text) {
	CHECK(parseDate("1/2/2024").has_value());
	CHECK_EQ(formatDate(*parseDate("29/02/2024")), "29/02/2024");
	CHECK(!parseDate("29/02/2023"));
	CHECK(!parseDate("31/04/2024"));
	CHECK(!parseDate("1/2/24"));
	CHECK(!parseDate("01/02/2024 "));
}

TEST_CASE("values", input_into_declared_types) {
	CHECK_EQ(describe(parseInput("42", integerType())), "INTEGER:42");
	CHECK_EQ(describe(parseInput("-7", integerType())), "INTEGER:-7");
	CHECK_EQ(describe(parseInput("+7", realType())), "REAL:7.0");
	CHECK_EQ(describe(parseInput("W", charType())), "CHAR:W");
	CHECK_EQ(describe(parseInput("", stringType())), "STRING:");
	CHECK_EQ(describe(parseInput("true", booleanType())), "BOOLEAN:TRUE");
	CHECK_EQ(describe(parseInput("1/2/2024", dateType())), "DATE:01/02/2024");
	CHECK_ERROR("R002", parseInput("4.5", integerType()));
	CHECK_ERROR("R002", parseInput("5.", realType()));
	CHECK_ERROR("R002", parseInput("ab", charType()));
	CHECK_ERROR("R002", parseInput("", charType()));
	CHECK_ERROR("R002", parseInput("yes", booleanType()));
	CHECK_ERROR("R002", parseInput("31/02/2024", dateType()));
	CHECK_ERROR("R005", parseInput("99999999999999999999", integerType()));
}

TEST_CASE("values", input_into_undeclared_names) {
	CHECK_EQ(describe(parseInput("12", nullptr)), "INTEGER:12");
	CHECK_EQ(describe(parseInput("4.5", nullptr)), "REAL:4.5");
	CHECK_EQ(describe(parseInput("FALSE", nullptr)), "BOOLEAN:FALSE");
	CHECK_EQ(describe(parseInput("W", nullptr)), "CHAR:W");
	CHECK_EQ(describe(parseInput("Hello there", nullptr)), "STRING:Hello there");
	CHECK_EQ(describe(parseInput("", nullptr)), "STRING:");
	CHECK_EQ(describe(parseInput("02/01/2005", nullptr)), "STRING:02/01/2005");
}

TEST_CASE("values", implicit_conversions) {
	CHECK_EQ(describe(convertTo(realType(), I(3))), "REAL:3.0");
	CHECK_EQ(describe(convertTo(stringType(), C('x'))), "STRING:x");
	CHECK_ERROR("R002", convertTo(integerType(), R(3.5)));
	CHECK_ERROR("R002", convertTo(charType(), S("x")));
	CHECK_ERROR("R002", convertTo(stringType(), I(1)));
	CHECK(isUnassigned(convertTo(integerType(), Value{})));
}

TEST_CASE("values", undeclared_variable_takes_its_first_type) {
	Cell cell;
	storeValue(cell, C('W'));
	CHECK(cell.type == charType());
	CHECK_ERROR("R002", storeValue(cell, S("Hello")));
	storeValue(cell, C('X'));
	CHECK_EQ(describe(cell.value), "CHAR:X");
}

TEST_CASE("values", arrays) {
	auto compiled = compile("DECLARE Grid : ARRAY[1:3, 0:1] OF INTEGER\nDECLARE Line : ARRAY[0:5] OF REAL\n");
	CHECK(compiled->clean);
	TypeTable& types = compiled->types;
	const DeclareStmt& gridDecl = compiled->declare(0);
	const Type* gridType = types.resolve(gridDecl.type);
	CHECK_EQ(typeName(gridType), "2D ARRAY OF INTEGER");

	CellPtr grid = makeCell(gridType, &gridDecl.type, literalBound, false);
	ArrayValue& array = *std::get<ArrayRef>(grid->value);
	CHECK_EQ(array.elements.size(), size_t{ 6 });
	CHECK_EQ(array.boundsText(), "[1:3, 0:1]");
	storeValue(*array.element({ 2, 1 }, "Grid"), I(9));
	CHECK_EQ(describe(array.element({ 2, 1 }, "Grid")->value), "INTEGER:9");
	CHECK_ERROR("R003", array.element({ 4, 0 }, "Grid"));
	CHECK_ERROR("R003", array.element({ 1, -1 }, "Grid"));
	CHECK_ERROR("R002", array.element({ 1 }, "Grid"));
	CHECK(isUnassigned(array.element({ 1, 0 }, "Grid")->value));

	const DeclareStmt& lineDecl = compiled->declare(1);
	CellPtr line = makeCell(types.resolve(lineDecl.type), &lineDecl.type, literalBound, true);
	ArrayValue& lineArray = *std::get<ArrayRef>(line->value);
	CHECK_EQ(describe(lineArray.element({ 0 }, "Line")->value), "REAL:0.0");
	storeValue(*lineArray.element({ 1 }, "Line"), I(4));
	CHECK_EQ(describe(lineArray.element({ 1 }, "Line")->value), "REAL:4.0");
	CHECK_ERROR("R002", storeValue(*lineArray.element({ 0 }, "Line"), S("x")));
	CHECK_ERROR("R002", storeValue(*line, grid->value));
}

TEST_CASE("values", array_bounds_are_checked) {
	auto compiled = compile("DECLARE A : ARRAY[5:1] OF INTEGER\nDECLARE B : ARRAY[1:3000, 1:3000] OF INTEGER\n");
	const DeclareStmt& backwards = compiled->declare(0);
	const DeclareStmt& huge = compiled->declare(1);
	CHECK_ERROR("R003", makeCell(compiled->types.resolve(backwards.type), &backwards.type, literalBound, false));
	CHECK_ERROR("R003", makeCell(compiled->types.resolve(huge.type), &huge.type, literalBound, false));
}

TEST_CASE("values", whole_array_assignment_copies_in_place) {
	auto compiled = compile("DECLARE Grid : ARRAY[1:2, 1:2] OF INTEGER\nDECLARE Saved : ARRAY[0:1, 0:1] OF INTEGER\n");
	const DeclareStmt& gridDecl = compiled->declare(0);
	const DeclareStmt& savedDecl = compiled->declare(1);
	CellPtr grid = makeCell(compiled->types.resolve(gridDecl.type), &gridDecl.type, literalBound, false);
	CellPtr saved = makeCell(compiled->types.resolve(savedDecl.type), &savedDecl.type, literalBound, false);
	storeValue(*std::get<ArrayRef>(grid->value)->element({ 2, 1 }, "Grid"), I(9));

	// A pointer into the target stays valid across the copy.
	CellPtr target = std::get<ArrayRef>(saved->value)->element({ 1, 0 }, "Saved");
	PointerValue pointer{ compiled->types.pointerTo(integerType()), target };
	storeValue(*saved, grid->value);
	storeValue(*std::get<ArrayRef>(grid->value)->element({ 2, 1 }, "Grid"), I(1));
	CHECK_EQ(describe(dereference(pointer)->value), "INTEGER:9");
	CHECK_EQ(std::get<ArrayRef>(saved->value)->boundsText(), "[0:1, 0:1]");
}

TEST_CASE("values", records_copy_on_assignment) {
	auto compiled = compile(R"(TYPE Point
   DECLARE X : INTEGER
   DECLARE Tags : ARRAY[1:3] OF STRING
ENDTYPE
)");
	CHECK(compiled->clean);
	const Type* point = compiled->types.find("Point");
	CellPtr p1 = makeCell(point, nullptr, literalBound, false);
	CellPtr p2 = makeCell(point, nullptr, literalBound, false);
	RecordValue& record = *std::get<RecordRef>(p1->value);
	storeValue(*record.fields[static_cast<size_t>(point->findField("x"))], I(3));
	storeValue(*std::get<ArrayRef>(record.fields[1]->value)->element({ 2 }, "Tags"), C('t'));
	storeValue(*p2, p1->value);
	storeValue(*record.fields[0], I(4));
	RecordValue& copy = *std::get<RecordRef>(p2->value);
	CHECK_EQ(describe(copy.fields[0]->value), "INTEGER:3");
	CHECK_EQ(describe(std::get<ArrayRef>(copy.fields[1]->value)->element({ 2 }, "Tags")->value), "STRING:t");
	CHECK_ERROR("R002", formatValue(p1->value));
}

TEST_CASE("values", pointers) {
	auto compiled = compile("TYPE TIntPointer = ^INTEGER\n");
	TypeTable& types = compiled->types;
	const Type* intPointer = types.find("TIntPointer");
	CellPtr pointerVariable = makeCell(intPointer, nullptr, literalBound, false);
	CHECK_ERROR("R008", dereference(std::get<PointerValue>(pointerVariable->value)));
	{
		CellPtr local = std::make_shared<Cell>(Cell{ integerType(), I(5) });
		storeValue(*pointerVariable, PointerValue{ types.pointerTo(integerType()), local });
		CHECK(typeOf(pointerVariable->value) == intPointer);
		storeValue(*dereference(std::get<PointerValue>(pointerVariable->value)), I(6));
		CHECK_EQ(describe(local->value), "INTEGER:6");
	}
	CHECK_ERROR("R008", dereference(std::get<PointerValue>(pointerVariable->value)));
	CHECK_ERROR("R002", storeValue(*pointerVariable, PointerValue{ types.pointerTo(realType()), {} }));
	CHECK_ERROR("R002", formatValue(pointerVariable->value));
}

TEST_CASE("values", objects_are_references) {
	auto compiled = compile(R"(CLASS Pet
   PRIVATE Name : STRING
ENDCLASS
CLASS Cat INHERITS Pet
ENDCLASS
)");
	const Type* pet = compiled->types.find("Pet");
	const Type* cat = compiled->types.find("Cat");
	CellPtr petVariable = makeCell(pet, nullptr, literalBound, false);
	CHECK(std::get<ObjectRef>(petVariable->value) == nullptr);
	Value kitty = makeObject(cat, literalBound, false);
	storeValue(*petVariable, kitty);
	CHECK(std::get<ObjectRef>(petVariable->value) == std::get<ObjectRef>(kitty));
	CellPtr catVariable = makeCell(cat, nullptr, literalBound, false);
	CHECK_ERROR("R002", storeValue(*catVariable, makeObject(pet, literalBound, false)));
	CHECK_ERROR("R002", formatValue(kitty));
}

TEST_CASE("values", defaults_flag_starting_values) {
	auto compiled = compile(R"(TYPE Season = (Spring, Summer)
TYPE Letters = SET OF CHAR
)");
	TypeTable& types = compiled->types;
	auto start = [&](const Type* type) { return describe(makeValue(type, nullptr, literalBound, true)); };
	CHECK_EQ(start(integerType()), "INTEGER:0");
	CHECK_EQ(start(realType()), "REAL:0.0");
	CHECK_EQ(start(charType()), "CHAR: ");
	CHECK_EQ(start(stringType()), "STRING:");
	CHECK_EQ(start(booleanType()), "BOOLEAN:FALSE");
	CHECK_EQ(start(dateType()), "DATE:01/01/0001");
	CHECK_EQ(start(types.find("Season")), "Season:Spring");
	CHECK(std::holds_alternative<SetRef>(makeValue(types.find("Letters"), nullptr, literalBound, true)));
	CHECK(isUnassigned(makeValue(integerType(), nullptr, literalBound, false)));
}
