#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"
#include "Environment.h"
#include "Types.h"
#include "Value.h"

// The semantic phase (spec 12): checks the whole program before any of it
// runs, so a program with a semantic error produces no output.
//
// It follows names and types through every statement the way the program
// would run them, reporting what can be known without running:
//   M001 undeclared names (strict mode); unknown subroutines, classes, fields
//   M002 names declared twice in one scope, or clashing with a type,
//        subroutine, enumerated value or built-in
//   M003 wrong number of arguments
//   M004 type mismatches between known types
//   M005 CALL on a function, or a procedure used in an expression
//   M006 non-variables or constants passed BYREF
//   M007 assignment to a constant, or to a FOR variable inside its loop
//   M008 RETURN outside a function, SUPER outside a method
//   M009 private members used from outside their class
// and the warnings
//   W001 one identifier written with different capitalisation
//   W002 overlapping CASE labels
//   W003 a variable declared but never used
//
// A type is unknown when it depends on running the program, for example the
// type of an undeclared variable, which is fixed by its first value. Checks
// involving an unknown type pass; the runtime makes them instead.
//
// Definitions themselves (TYPE, CLASS, subroutine headers) are checked when
// the TypeTable and Environment are built.
class Analyzer {
public:
	struct Options {
		bool strict = false; // --strict: every variable must be declared (spec 5.1)
	};

	Analyzer(const Program& program, TypeTable& types, const Environment& environment, Options options);

	void analyze();

	const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
	struct Symbol {
		enum class Kind { Variable, Constant, Parameter, Implicit };

		Symbol(Kind kind, std::string name, const Type* type, SourceLocation location, std::optional<Value> value = {})
			: kind(kind), name(std::move(name)), type(type), location(location), value(std::move(value)) {}

		Kind kind;
		std::string name;      // the spelling where it was declared or first used
		const Type* type;      // nullptr if not known before running
		SourceLocation location;
		std::optional<Value> value; // a constant's value, for CASE label checks
		bool used = false;
	};

	struct Scope {
		std::map<std::string, Symbol> symbols; // keyed by upper-case name
	};

	// What a name means where it is used (spec 5.8).
	struct Resolved {
		enum class Kind { Symbol, Attribute, Method, EnumValue, Subroutine, Type, Builtin, None };
		Kind kind = Kind::None;
		Symbol* symbol = nullptr;
		const Field* field = nullptr;
		const Method* method = nullptr;
		const Subroutine* subroutine = nullptr;
		const Type* type = nullptr; // the type of a value, or the named TYPE or CLASS
		std::string name;           // as defined
		std::optional<EnumValue> enumValue;
	};

	// Where the analysis is: a subroutine, a method or the main program.
	struct Context {
		const Subroutine* subroutine = nullptr; // null in the main program and in initialisers
		const Type* methodClass = nullptr;      // inside a class
		Scope* locals = nullptr;                // null in the main program
	};

	// Declarations
	void collectGlobals(const Block& block);
	void declareGlobal(const Stmt& stmt);
	bool checkNameIsFree(const Identifier& name);
	Symbol* addSymbol(Scope& scope, Symbol symbol, bool reportDuplicate);
	void analyzeSubroutine(const Subroutine& subroutine, const Type* methodClass);
	void reportUnused(const Scope& scope);

	// Statements
	void block(const Block& statements);
	void statement(const Stmt& stmt);
	void declare(const DeclareStmt& node, const Stmt& stmt);
	void assign(const AssignStmt& node);
	void callStatement(const CallStmt& node, const Stmt& stmt);
	void caseStatement(const CaseStmt& node);
	void forStatement(const ForStmt& node);
	void returnStatement(const ReturnStmt& node, const Stmt& stmt);
	void fileStatement(const Stmt& stmt);
	void define(const DefineStmt& node, const Stmt& stmt);
	void requireType(const Expr& expr, TypeKind kind, std::string_view what);
	void requireFileName(const Expr& expr);

	// Expressions; each returns the static type, or nullptr if unknown.
	const Type* expression(const Expr& expr);
	const Type* name(const NameExpr& node, SourceLocation location);
	const Type* target(const Expr& expr, const Type* typeIfNew = nullptr);
	const Type* index(const IndexExpr& node, SourceLocation location);
	const Type* member(const MemberExpr& node);
	const Type* call(const Expr& callee, const std::vector<ExprPtr>& arguments, SourceLocation location, bool asStatement,
		bool usesCallKeyword);
	const Type* construct(const NewExpr& node, SourceLocation location);
	void checkArguments(const Subroutine& subroutine, const std::vector<ExprPtr>& arguments, SourceLocation location,
		std::string_view what);
	std::optional<Value> constantValue(const Expr& expr);

	// Names
	Resolved resolve(const std::string& written, SourceLocation location);
	void noteSpelling(std::string_view written, std::string_view canonical, SourceLocation location,
		SourceLocation definedAt);
	void noteTypeSpelling(const TypeSpec& spec);
	bool isLoopVariable(std::string_view name) const;
	bool reportHiddenPrivate(const std::string& written, SourceLocation location);

	void error(const char* code, SourceLocation location, std::string message);
	void warning(const char* code, SourceLocation location, std::string message);

	const Program& program_;
	TypeTable& types_;
	const Environment& environment_;
	Options options_;
	Scope globals_;
	Context context_;
	std::vector<std::string> loopVariables_; // upper-case names of the enclosing FOR loops
	std::set<std::string> spellingsWarned_;
	std::vector<Diagnostic> diagnostics_;
};
