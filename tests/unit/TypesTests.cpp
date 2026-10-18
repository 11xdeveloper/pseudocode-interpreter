#include <algorithm>

#include "Helpers.h"
#include "TestFramework.h"

// The type table and the type rules (spec 4, 5.4-5.7, 11).

namespace {

// The diagnostics' codes, sorted and joined: "M001 M002 M002".
std::string codes(const std::vector<Diagnostic>& diagnostics) {
	std::vector<std::string> sorted;
	for (const Diagnostic& diagnostic : diagnostics) {
		sorted.push_back(diagnostic.code);
	}
	std::sort(sorted.begin(), sorted.end());
	std::string result;
	for (const std::string& code : sorted) {
		result += (result.empty() ? "" : " ") + code;
	}
	return result;
}

}

TEST_CASE("types", definitions_resolve) {
	auto compiled = compile(R"(TYPE Season = (Spring, Summer, Autumn, Winter)
TYPE NodePtr = ^Node
TYPE Node
   DECLARE Value : INTEGER
   DECLARE Link : NodePtr
ENDTYPE
TYPE Letters = SET OF CHAR
CLASS Pet
   PRIVATE Name : STRING
   PUBLIC FUNCTION Speak() RETURNS STRING
      RETURN "..."
   ENDFUNCTION
ENDCLASS
CLASS Cat INHERITS Pet
   PRIVATE Breed : STRING
   PUBLIC FUNCTION Speak() RETURNS STRING
      RETURN "Meow"
   ENDFUNCTION
ENDCLASS
)");
	CHECK(compiled->clean);
	TypeTable& types = compiled->types;
	const Type* season = types.find("SEASON");
	CHECK(season && season->kind == TypeKind::Enum && season->enumValues.size() == 4);
	auto winter = types.findEnumValue("winter");
	CHECK(winter && winter->type == season && winter->ordinal == 3);

	const Type* node = types.find("Node");
	const Type* nodePointer = types.find("NodePtr");
	CHECK(nodePointer->element == node);
	CHECK(node->fields[static_cast<size_t>(node->findField("link"))].type == nodePointer);
	CHECK(types.find("Letters")->element == charType());

	const Type* pet = types.find("Pet");
	const Type* cat = types.find("Cat");
	CHECK(cat->isA(pet) && !pet->isA(cat));
	CHECK_EQ(cat->fields.size(), size_t{ 2 });
	CHECK(cat->fields[0].owner == pet); // inherited attributes come first
	CHECK(cat->findMethod("speak")->owner == cat);
	CHECK(pet->findMethod("SPEAK")->owner == pet);
}

TEST_CASE("types", definition_errors) {
	auto compiled = compile(R"(TYPE Season = (Spring, Summer)
TYPE Weather = (Sunny, Spring)
TYPE Season = (A, B)
TYPE Ptr = ^Missing
TYPE BadSet = SET OF Node
TYPE Node
   DECLARE Value : INTEGER
   DECLARE Value : STRING
   DECLARE Children : ARRAY[1:2] OF Node
ENDTYPE
TYPE Bad
   DECLARE S : Spring
ENDTYPE
CLASS A INHERITS B
ENDCLASS
CLASS B INHERITS A
ENDCLASS
CLASS C INHERITS Season
ENDCLASS
CLASS D INHERITS Nowhere
   PRIVATE Name : STRING
   PUBLIC PROCEDURE Name()
   ENDPROCEDURE
ENDCLASS
CLASS Self INHERITS Self
ENDCLASS
)");
	// M001: Missing, Spring used as a type, Nowhere.
	// M002: Spring twice, Season twice, field Value twice, method Name clashing with attribute Name.
	// M004: SET OF a record, A/B cycle, Season not a class, Self inheriting itself, Node containing itself.
	CHECK_EQ(codes(compiled->types.diagnostics()), "M001 M001 M001 M002 M002 M002 M002 M004 M004 M004 M004 M004");
}

TEST_CASE("types", subroutine_definition_errors) {
	auto compiled = compile(R"(TYPE Season = (Spring, Summer)
TYPE LENGTH = (A1, B1)
PROCEDURE Spring()
ENDPROCEDURE
PROCEDURE Swap(BYREF X : INTEGER, x : INTEGER)
ENDPROCEDURE
PROCEDURE Swap()
ENDPROCEDURE
FUNCTION Bad(BYREF N : INTEGER) RETURNS INTEGER
   RETURN N
ENDFUNCTION
PROCEDURE MID()
ENDPROCEDURE
)");
	// M002: type LENGTH, procedure Spring, parameter x twice, Swap twice, procedure MID. M006: BYREF in a function.
	CHECK_EQ(codes(compiled->environment->diagnostics()), "M002 M002 M002 M002 M002 M006");
}

TEST_CASE("types", structural_arrays_and_pointers) {
	auto compiled = compile("TYPE TIntPointer = ^INTEGER\nTYPE Other = ^INTEGER\n");
	TypeTable& types = compiled->types;
	CHECK(types.arrayOf(integerType(), 1) == types.arrayOf(integerType(), 1));
	CHECK(types.arrayOf(integerType(), 1) != types.arrayOf(integerType(), 2));
	CHECK(sameType(types.find("TIntPointer"), types.pointerTo(integerType())));
	CHECK(sameType(types.find("TIntPointer"), types.find("Other")));
	CHECK(!sameType(types.pointerTo(integerType()), types.pointerTo(realType())));
}

TEST_CASE("types", nominal_records) {
	auto compiled = compile(R"(TYPE A
   DECLARE X : INTEGER
ENDTYPE
TYPE B
   DECLARE X : INTEGER
ENDTYPE
)");
	CHECK(!sameType(compiled->types.find("A"), compiled->types.find("B")));
	CHECK(!isAssignable(compiled->types.find("A"), compiled->types.find("B")));
}

TEST_CASE("types", assignability) {
	auto compiled = compile(R"(CLASS Pet
ENDCLASS
CLASS Cat INHERITS Pet
ENDCLASS
)");
	TypeTable& types = compiled->types;
	CHECK(isAssignable(realType(), integerType()));
	CHECK(isAssignable(stringType(), charType()));
	CHECK(!isAssignable(integerType(), realType()));
	CHECK(!isAssignable(charType(), stringType()));
	CHECK(!isAssignable(stringType(), integerType()));
	CHECK(isAssignable(types.find("Pet"), types.find("Cat")));
	CHECK(!isAssignable(types.find("Cat"), types.find("Pet")));
	CHECK(isAssignable(types.arrayOf(stringType(), 0), types.arrayOf(stringType(), 2)));
	CHECK(!isAssignable(types.arrayOf(realType(), 1), types.arrayOf(integerType(), 1)));
	CHECK(isAssignable(nullptr, integerType())); // unknown types pass
	CHECK_EQ(assignmentError(realType(), integerType()), "");
	CHECK(assignmentError(integerType(), realType()).find("INT()") != std::string::npos);
}

TEST_CASE("types", operator_rules) {
	using enum BinaryOp;
	CHECK(checkBinary(Divide, integerType(), integerType()).result == realType());
	CHECK(checkBinary(Add, integerType(), integerType()).result == integerType());
	CHECK(checkBinary(Add, integerType(), realType()).result == realType());
	CHECK(checkBinary(Concat, charType(), stringType()).result == stringType());
	CHECK(checkBinary(Less, integerType(), realType()).result == booleanType());
	CHECK(!checkBinary(Less, booleanType(), booleanType()).ok());
	CHECK(checkBinary(Equal, booleanType(), booleanType()).ok());
	CHECK(!checkBinary(IntDiv, realType(), integerType()).ok());
	CHECK(!checkBinary(Equal, integerType(), stringType()).ok());
	CHECK(checkBinary(Add, integerType(), nullptr).ok()); // unknown passes
	CHECK(!checkBinary(Concat, integerType(), nullptr).ok()); // a known bad side still fails
	CHECK(!checkUnary(UnaryOp::Not, integerType()).ok());
	CHECK(checkUnary(UnaryOp::Negate, realType()).result == realType());
}

TEST_CASE("types", descriptions) {
	auto compiled = compile("TYPE Season = (Spring)\n");
	CHECK_EQ(describeType(integerType()), "an INTEGER");
	CHECK_EQ(describeType(stringType()), "a STRING");
	CHECK_EQ(describeType(compiled->types.arrayOf(charType(), 1)), "an ARRAY OF CHAR");
	CHECK_EQ(describeType(compiled->types.arrayOf(charType(), 2)), "a 2D ARRAY OF CHAR");
	CHECK_EQ(describeType(compiled->types.find("Season")), "a Season value");
}
