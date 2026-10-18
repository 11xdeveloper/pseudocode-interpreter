#include "Helpers.h"
#include "Operations.h"
#include "TestFramework.h"

// Scopes, names, declarations and parameter passing (spec 5, 8, 11).

namespace {

const char* const Source = R"(CONSTANT Limit = 3
TYPE Season = (Spring, Summer)
PROCEDURE Swap(BYREF X : INTEGER, Y : INTEGER)
ENDPROCEDURE
PROCEDURE Fill(Names : ARRAY[1:Limit] OF STRING, Any : ARRAY OF STRING)
ENDPROCEDURE
FUNCTION Half(N : INTEGER) RETURNS REAL
   RETURN N / 2
ENDFUNCTION
PROCEDURE Scale(BYREF R : REAL)
ENDPROCEDURE
FUNCTION Fact(N : INTEGER) RETURNS INTEGER
   RETURN N
ENDFUNCTION
CLASS Pet
   PRIVATE Name : STRING
   PUBLIC Legs : INTEGER
   PUBLIC PROCEDURE NEW(GivenName : STRING)
   ENDPROCEDURE
   PUBLIC FUNCTION Speak() RETURNS STRING
      RETURN "..."
   ENDFUNCTION
   PRIVATE PROCEDURE Secret()
   ENDPROCEDURE
ENDCLASS
CLASS Cat INHERITS Pet
   PRIVATE Breed : STRING
   PUBLIC FUNCTION Speak() RETURNS STRING
      RETURN "Meow"
   ENDFUNCTION
ENDCLASS
DECLARE Names : ARRAY[1:3] OF STRING
DECLARE Short : ARRAY[1:2] OF STRING
)";

Identifier id(const char* name, int line = 1) {
	return { name, { line, 1 } };
}

// An environment for Source, with Limit defined and the two arrays declared.
struct Setup {
	std::unique_ptr<Compiled> compiled;
	Environment& env;
	BoundEvaluator bound;
	CellPtr names;
	CellPtr shortNames;

	explicit Setup(Environment::Options options = {})
		: compiled(compile(Source, options)), env(*compiled->environment) {
		bound = [this](const Expr& expr) -> std::int64_t {
			if (const auto* name = std::get_if<NameExpr>(&expr.node)) {
				return requireInteger(env.read(id(name->name.c_str())), "A bound");
			}
			return literalBound(expr);
		};
		const auto& limit = std::get<ConstantStmt>(compiled->program.statements[0]->node);
		env.defineConstant(limit.name, I(3), &limit);
		names = declare(1);
		shortNames = declare(2);
	}

	CellPtr declare(size_t statement) {
		const DeclareStmt& node = compiled->declare(statement);
		return env.declare(node.name, compiled->types.resolve(node.type), &node.type, bound, &node);
	}

	const Subroutine& subroutine(const char* name) {
		return *env.findSubroutine(name);
	}
};

}

TEST_CASE("environment", program_is_clean) {
	Setup setup;
	CHECK(setup.compiled->clean);
}

TEST_CASE("environment", constants) {
	Setup setup;
	CHECK_EQ(describe(setup.env.read(id("LIMIT"))), "INTEGER:3");
	CHECK_ERROR("M007", setup.env.cellFor(id("Limit")));
	// Running the same CONSTANT statement again, as in a loop, is allowed.
	const auto& limit = std::get<ConstantStmt>(setup.compiled->program.statements[0]->node);
	setup.env.defineConstant(limit.name, I(3), &limit);
}

TEST_CASE("environment", declarations) {
	Setup setup;
	Environment& env = setup.env;
	CHECK_ERROR("M002", env.declare(id("names", 40), stringType(), nullptr, setup.bound, nullptr));
	CHECK_ERROR("M002", env.declare(id("Swap"), integerType(), nullptr, setup.bound, nullptr));
	CHECK_ERROR("M002", env.declare(id("Summer"), integerType(), nullptr, setup.bound, nullptr));
	CHECK_ERROR("M002", env.declare(id("Length"), integerType(), nullptr, setup.bound, nullptr));

	int site = 0;
	CellPtr count = env.declare(id("Count", 30), integerType(), nullptr, setup.bound, &site);
	CHECK_ERROR("R001", env.read(id("count")));
	storeValue(*count, I(5));
	CellPtr again = env.declare(id("Count", 30), integerType(), nullptr, setup.bound, &site);
	CHECK(again != count);
	CHECK(isUnassigned(again->value));
}

TEST_CASE("environment", undeclared_variables) {
	Setup setup;
	Environment& env = setup.env;
	CellPtr temp = env.cellFor(id("Temp", 7));
	storeValue(*temp, C('x'));
	CHECK_EQ(env.lookup("TEMP")->name, "Temp");
	CHECK(temp->type == charType());
	CHECK_ERROR("R001", env.read(id("Nowhere")));
	int site = 0;
	CHECK_ERROR("M002", env.declare(id("Temp", 50), charType(), nullptr, setup.bound, &site));
	CHECK(env.cellFor(id("I"), integerType())->type == integerType());
}

TEST_CASE("environment", what_names_mean) {
	Setup setup;
	Environment& env = setup.env;
	CHECK_EQ(describe(env.read(id("summer"))), "Season:Summer");
	CHECK_ERROR("M005", env.read(id("Swap")));
	CHECK_ERROR("M005", env.read(id("Half")));
	CHECK_ERROR("M004", env.read(id("Season")));
	CHECK_ERROR("M007", env.cellFor(id("Spring")));
	CHECK_ERROR("M002", env.cellFor(id("Half")));
	CHECK(env.lookup("mid")->kind == NameBinding::Kind::Builtin);
}

TEST_CASE("environment", byref_shares_the_callers_cell) {
	Setup setup;
	Environment& env = setup.env;
	CellPtr a = env.cellFor(id("A"));
	CellPtr b = env.cellFor(id("B"));
	storeValue(*a, I(1));
	storeValue(*b, I(2));
	std::vector<Argument> arguments(2);
	arguments[0].cell = a;
	arguments[1].cell = b; // Y is BYREF too: the keyword carries on (spec 8.3)
	env.enterCall({ &setup.subroutine("Swap"), nullptr, nullptr }, std::move(arguments), setup.bound);
	{
		CallScope scope(env);
		storeValue(*env.cellFor(id("X")), I(99));
		CHECK(env.lookup("X")->cell == a);
		CHECK(env.lookup("A")->cell == a); // globals stay visible inside the call
	}
	CHECK_EQ(describe(a->value), "INTEGER:99");
	CHECK_EQ(env.depth(), size_t{ 0 });
}

TEST_CASE("environment", byref_argument_errors) {
	Setup setup;
	Environment& env = setup.env;
	CellPtr a = env.cellFor(id("A"));
	storeValue(*a, I(1));
	{
		std::vector<Argument> arguments(2);
		arguments[0].cell = a;
		arguments[1].value = I(2); // an expression for a BYREF parameter
		CHECK_ERROR("M006", env.enterCall({ &setup.subroutine("Swap"), nullptr, nullptr }, std::move(arguments), setup.bound));
		CHECK_EQ(env.depth(), size_t{ 0 }); // a failed call pushes no frame
	}
	{
		std::vector<Argument> arguments(2);
		arguments[0].cell = a;
		arguments[1].cell = env.lookup("Limit")->cell;
		arguments[1].isConstant = true;
		CHECK_ERROR("M006", env.enterCall({ &setup.subroutine("Swap"), nullptr, nullptr }, std::move(arguments), setup.bound));
	}
	{
		std::vector<Argument> arguments(1);
		arguments[0].cell = a;
		CHECK_ERROR("R002", env.enterCall({ &setup.subroutine("Scale"), nullptr, nullptr }, std::move(arguments), setup.bound));
	}
	{
		CellPtr undeclared = env.cellFor(id("Result"));
		std::vector<Argument> arguments(1);
		arguments[0].cell = undeclared;
		env.enterCall({ &setup.subroutine("Scale"), nullptr, nullptr }, std::move(arguments), setup.bound);
		env.exitCall();
		CHECK(undeclared->type == realType()); // takes the parameter's type
	}
}

TEST_CASE("environment", byval_arguments_and_returns) {
	Setup setup;
	Environment& env = setup.env;
	const Subroutine& half = setup.subroutine("Half");
	CHECK_ERROR("M003", env.enterCall({ &half, nullptr, nullptr }, {}, setup.bound));
	std::vector<Argument> real(1);
	real[0].value = R(2.5);
	CHECK_ERROR("R002", env.enterCall({ &half, nullptr, nullptr }, std::move(real), setup.bound));
	std::vector<Argument> unassigned(1);
	CHECK_ERROR("R001", env.enterCall({ &half, nullptr, nullptr }, std::move(unassigned), setup.bound));
	CHECK_EQ(describe(env.returnValue(half, I(3))), "REAL:3.0");
	CHECK_ERROR("R002", env.returnValue(half, S("x")));
}

TEST_CASE("environment", array_parameters) {
	Setup setup;
	Environment& env = setup.env;
	const Subroutine& fill = setup.subroutine("Fill");
	storeValue(*std::get<ArrayRef>(setup.names->value)->element({ 1 }, "Names"), S("Ann"));
	{
		std::vector<Argument> arguments(2);
		arguments[0].value = setup.names->value;
		arguments[1].value = setup.shortNames->value;
		env.enterCall({ &fill, nullptr, nullptr }, std::move(arguments), setup.bound);
		CallScope scope(env);
		CellPtr local = env.cellFor(id("Names"));
		CHECK(local != setup.names); // the parameter hides the global
		storeValue(*std::get<ArrayRef>(local->value)->element({ 1 }, "Names"), S("Bob"));
		CHECK_EQ(describe(std::get<ArrayRef>(setup.names->value)->element({ 1 }, "Names")->value), "STRING:Ann");
		CHECK_EQ(std::get<ArrayRef>(env.lookup("Any")->cell->value)->bounds[0].upper, std::int64_t{ 2 });
	}
	std::vector<Argument> wrongBounds(2);
	wrongBounds[0].value = setup.shortNames->value;
	wrongBounds[1].value = setup.names->value;
	CHECK_ERROR("R002", env.enterCall({ &fill, nullptr, nullptr }, std::move(wrongBounds), setup.bound));
}

TEST_CASE("environment", locals_end_with_their_call) {
	Setup setup;
	Environment& env = setup.env;
	PointerValue toLocal{ setup.compiled->types.pointerTo(integerType()), {} };
	int site = 0;
	{
		std::vector<Argument> arguments(1);
		arguments[0].value = I(1);
		env.enterCall({ &setup.subroutine("Fact"), nullptr, nullptr }, std::move(arguments), setup.bound);
		CallScope scope(env);
		env.declare(id("Count", 13), integerType(), nullptr, setup.bound, &site);
		CellPtr made = env.cellFor(id("Made"));
		storeValue(*made, I(4));
		toLocal.target = made;
	}
	CHECK(!env.lookup("Made"));
	CHECK(!env.lookup("Count"));
	CHECK_ERROR("R008", dereference(toLocal));
}

TEST_CASE("environment", call_depth_and_chain) {
	Environment::Options options;
	options.maxDepth = 50;
	Setup setup(options);
	Environment& env = setup.env;
	std::vector<std::unique_ptr<CallScope>> scopes;
	int line = 20;
	bool stopped = false;
	try {
		while (true) {
			env.setCurrentLine(line);
			std::vector<Argument> arguments(1);
			arguments[0].value = I(1);
			env.enterCall({ &setup.subroutine("Fact"), nullptr, nullptr }, std::move(arguments), setup.bound);
			scopes.push_back(std::make_unique<CallScope>(env));
			line = 13;
		}
	}
	catch (const RuntimeError& error) {
		stopped = true;
		CHECK_EQ(error.code(), "R011");
		CHECK_EQ(env.depth(), size_t{ 50 });
		std::vector<std::string> chain = env.callChain();
		CHECK_EQ(chain.size(), size_t{ 50 });
		CHECK_EQ(chain.back(), "in function Fact (line 13), called from line 20");
		CHECK_EQ(chain.front(), "in function Fact (line 13), called from line 13");
	}
	CHECK(stopped);
	scopes.clear();
	CHECK_EQ(env.depth(), size_t{ 0 });
}

TEST_CASE("environment", object_members_from_outside) {
	Setup setup;
	Environment& env = setup.env;
	const Type* pet = setup.compiled->types.find("Pet");
	const Type* cat = setup.compiled->types.find("Cat");
	ObjectRef kitty = makeObject(cat, setup.bound, false);
	CHECK(Environment::constructorOf(cat) == nullptr);
	CHECK(Environment::constructorOf(pet) != nullptr);
	CHECK_ERROR("M009", env.attribute(kitty, id("Name")));
	CHECK_ERROR("M009", env.method(kitty, id("Secret")));
	CHECK_ERROR("R008", env.attribute(ObjectRef{}, id("Legs")));
	CHECK_ERROR("M001", env.attribute(kitty, id("Tail")));
	storeValue(*env.attribute(kitty, id("legs")), I(4));
	CHECK(env.method(kitty, id("speak")).owner == cat);
	CHECK_ERROR("M008", env.superMethod(id("Speak")));
}

TEST_CASE("environment", inside_methods) {
	Setup setup;
	Environment& env = setup.env;
	const Type* pet = setup.compiled->types.find("Pet");
	const Type* cat = setup.compiled->types.find("Cat");
	ObjectRef kitty = makeObject(cat, setup.bound, false);
	{
		// Pet's method running on a Cat sees Pet's private members.
		const Method& speak = *pet->findMethod("Speak");
		env.enterCall({ speak.definition, kitty, pet }, {}, setup.bound);
		CallScope scope(env);
		auto name = env.lookup("Name");
		CHECK(name && name->kind == NameBinding::Kind::Attribute);
		CHECK(env.lookup("Legs")->cell == env.attribute(kitty, id("Legs")));
		CHECK(env.lookup("Secret")->kind == NameBinding::Kind::Method);
		CHECK(!env.lookup("Breed")); // a subclass's attribute is not visible to the parent
		CHECK(env.callChain().front().starts_with("in method Pet.Speak"));
	}
	{
		// Cat's method: the parent's private members are hidden (D15).
		const Method& speak = *cat->findMethod("Speak");
		env.enterCall({ speak.definition, kitty, cat }, {}, setup.bound);
		CallScope scope(env);
		CHECK(!env.lookup("Name"));
		CHECK(env.lookup("Breed")->kind == NameBinding::Kind::Attribute);
		CHECK_ERROR("M009", env.attribute(kitty, id("Name")));
		CHECK(env.superMethod(id("speak"))->owner == pet);
		CHECK(env.superMethod(id("NEW"))->owner == pet);
		CHECK_ERROR("M009", env.superMethod(id("Secret")));
	}
}

TEST_CASE("environment", strict_mode) {
	Environment::Options options;
	options.strict = true;
	Setup setup(options);
	CHECK_ERROR("M001", setup.env.cellFor(id("Total")));
	CHECK_ERROR("M001", setup.env.read(id("Total")));
}
