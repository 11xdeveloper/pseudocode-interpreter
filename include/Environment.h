#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"
#include "Types.h"
#include "Value.h"

// Variables, scopes, name resolution and calls (spec 5.1, 5.2, 5.8, 8, 11).
//
// The environment is a stack of frames. The first is the global frame; each
// procedure, function or method call pushes another. A frame has one scope:
// there is no block scope, so a variable declared inside an IF or loop
// belongs to the enclosing subroutine or the global scope (spec 5.8).
//
// Some checks here belong to the semantic phase, which catches them before
// the program runs. The environment repeats them so it is safe on its own;
// such errors are thrown as RuntimeError carrying their M code.

// The built-in function names of spec 9, which are reserved (spec 2.2).
bool isBuiltinFunction(std::string_view name);

struct Variable {
	std::string name; // spelling where it was declared or first assigned (spec 2.2)
	CellPtr cell;
	bool isConstant = false;
	SourceLocation location;
	// The DECLARE or CONSTANT statement that made it, so running that
	// statement again (in a loop) makes a fresh variable rather than being
	// reported as a redeclaration. Null for a parameter or an undeclared
	// variable created by assignment.
	const void* site = nullptr;
};

// The variables of one frame. Names are case-insensitive.
class Scope {
public:
	Variable* find(std::string_view name);
	const Variable* find(std::string_view name) const;
	Variable& set(Variable variable); // adds, or replaces one with the same name

private:
	std::map<std::string, Variable> variables_; // keyed by upper-case name
};

// A subroutine's parameter and return types, resolved once.
struct Signature {
	std::vector<const Type*> parameterTypes; // nullptr where the type is unknown
	const Type* returnType = nullptr;        // functions only
};

// What a name refers to (spec 5.8).
struct NameBinding {
	enum class Kind {
		Variable,   // local or global variable or constant
		Attribute,  // attribute of the current object, inside a method
		EnumValue,  // value of an enumerated type
		Subroutine, // top-level procedure or function
		Method,     // method of the current object, inside a method
		Type,       // TYPE or CLASS name
		Builtin,    // built-in function
	};

	Kind kind;
	std::string name;                   // spelling where it was defined
	CellPtr cell;                       // Variable, Attribute
	bool isConstant = false;            // Variable
	const Field* field = nullptr;       // Attribute
	std::optional<EnumValue> enumValue; // EnumValue
	const Subroutine* subroutine = nullptr; // Subroutine, Method
	const Method* method = nullptr;     // Method: the version the current class sees; dispatch with Environment::method
	const Type* type = nullptr;         // Type
};

// One evaluated argument of a call. For a BYVAL parameter the interpreter
// fills `value`; for a BYREF parameter, `cell` with the caller's storage, or
// leaves it null if the argument is not a variable, element or field.
struct Argument {
	Value value;
	CellPtr cell;
	bool isConstant = false; // the argument names a constant
};

// The code a frame runs.
struct CallTarget {
	const Subroutine* subroutine = nullptr; // null while running a class's attribute initialisers
	ObjectRef self;                         // the object a method or initialiser runs on
	const Type* methodClass = nullptr;      // the class declaring the method; null outside classes
};

struct Frame {
	CallTarget target;
	Scope locals;
	int callLine = 0;    // the line in the calling frame that made this call
	int currentLine = 0; // the line being executed, kept up to date by the interpreter
};

class Environment {
public:
	struct Options {
		bool strict = false;      // --strict: every variable must be declared (spec 5.1)
		bool useDefaults = false; // --defaults: variables start with zero values (spec 5.1)
		size_t maxDepth = 10000;  // --max-depth: the deepest call nesting allowed (spec 8.4)
	};

	// Registers the program's subroutines and resolves every signature.
	// Name clashes and invalid parameters are reported in diagnostics().
	Environment(const Program& program, TypeTable& types, Options options);
	Environment(const Environment&) = delete;
	Environment& operator=(const Environment&) = delete;

	const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
	const Options& options() const { return options_; }
	TypeTable& types() { return types_; }

	// ---------- names (spec 5.8) ----------

	// Resolves a name: the local scope, then (inside a method) the current
	// object's attributes and methods that its class can see, then the
	// global scope and the program's definitions, then the built-ins.
	std::optional<NameBinding> lookup(std::string_view name) const;

	// The value of a name used in an expression. Throws R001 for a variable
	// with no value yet, and M005 for a subroutine used without calling it.
	Value read(const Identifier& name) const;

	// The cell a name assigns to, for assignment, INPUT, READFILE, FOR and
	// BYREF arguments. In the default mode an unknown name becomes a new
	// variable in the current scope (spec 5.1), typed `typeIfNew` (INTEGER
	// for a FOR loop variable) or by its first value. Throws M007 for a
	// constant and M001 for an unknown name in strict mode.
	CellPtr cellFor(const Identifier& name, const Type* typeIfNew = nullptr);

	// ---------- declarations (spec 5.1, 5.2) ----------

	// DECLARE in the current scope: global at top level, local in a
	// subroutine. `site` is the DECLARE statement. Throws M002 if the name
	// is already used in this scope or names a type, subroutine or built-in.
	CellPtr declare(const Identifier& name, const Type* type, const TypeSpec* spec,
		const BoundEvaluator& evaluateBound, const void* site);

	// CONSTANT in the current scope. `value` is the literal's value.
	void defineConstant(const Identifier& name, const Value& value, const void* site);

	// ---------- subroutines and objects (spec 8, 11) ----------

	const Subroutine* findSubroutine(std::string_view name) const;
	const Signature& signature(const Subroutine& subroutine) const;

	// Object.Name and Object.Method from the running code. Private members
	// are only visible inside methods of the class that declares them
	// (spec 11.2). Throw R008 for a null object, M009 for a private member.
	CellPtr attribute(const ObjectRef& object, const Identifier& name) const;
	const Method& method(const ObjectRef& object, const Identifier& name) const;
	// SUPER.Name: the parent class's version (spec 11.4). Returns nullptr for
	// SUPER.NEW when the parent has no constructor of its own (spec 11.3).
	const Method* superMethod(const Identifier& name) const;
	// A class's own NEW, or nullptr for the implicit constructor. Constructors
	// are not inherited.
	static const Method* constructorOf(const Type* classType);

	// Record.Field (spec 5.6).
	CellPtr field(const RecordRef& record, const Identifier& name) const;

	// ---------- calls (spec 8.3, 8.4) ----------

	// Pushes a frame for a call and binds the arguments, which the caller has
	// already evaluated left to right. BYVAL parameters get a converted copy;
	// BYREF parameters share the argument's cell. Throws R011 when the calls
	// nest too deeply, and M003, M006 or R002 for arguments that do not fit.
	// The frame is only pushed if binding succeeds.
	void enterCall(const CallTarget& target, std::vector<Argument> arguments, const BoundEvaluator& evaluateBound);
	void exitCall();

	// A function's RETURN value, converted to its declared return type and
	// copied (spec 8.2). Throws R002 if it does not fit.
	Value returnValue(const Subroutine& function, const Value& value) const;

	const Frame& currentFrame() const { return *frames_.back(); }
	const Type* currentClass() const { return frames_.back()->target.methodClass; }
	bool atTopLevel() const { return frames_.size() == 1; }
	size_t depth() const { return frames_.size() - 1; }

	void setCurrentLine(int line) { frames_.back()->currentLine = line; }

	// The active calls, innermost first, for runtime error messages (spec 12.1):
	// "in procedure ClearNames (line 14), called from line 40". Capture it
	// where a runtime error is first caught, before CallScopes unwind.
	std::vector<std::string> callChain() const;

private:
	Scope& currentScope() { return frames_.back()->locals; }
	void checkNameIsFree(const Identifier& name) const;
	void bindParameters(const Subroutine& subroutine, std::vector<Argument>& arguments,
		const BoundEvaluator& evaluateBound, Scope& locals) const;
	void checkArrayBounds(const Parameter& parameter, const Type* type, const Value& value,
		const BoundEvaluator& evaluateBound) const;
	void registerSubroutine(const Subroutine& subroutine);
	void resolveSignature(const Subroutine& subroutine);
	void error(const char* code, SourceLocation location, std::string message);

	TypeTable& types_;
	Options options_;
	std::vector<std::unique_ptr<Frame>> frames_;
	std::map<std::string, const Subroutine*> subroutines_; // keyed by upper-case name
	std::map<const Subroutine*, Signature> signatures_;
	std::vector<Diagnostic> diagnostics_;
};

// Pops the frame pushed by Environment::enterCall when it goes out of scope,
// including when a runtime error unwinds through the call.
class CallScope {
public:
	explicit CallScope(Environment& environment) : environment_(environment) {}
	~CallScope() { environment_.exitCall(); }
	CallScope(const CallScope&) = delete;
	CallScope& operator=(const CallScope&) = delete;

private:
	Environment& environment_;
};
