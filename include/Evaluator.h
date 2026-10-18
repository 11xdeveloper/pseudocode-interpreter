#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"
#include "Environment.h"
#include "RuntimeError.h"
#include "Value.h"

// Runs a program by walking its AST (spec 6-8, 11).
//
// Expressions are evaluated with the operators of Operations.h and the names
// of the Environment; statements are executed in order. The first runtime
// error stops the program (spec 12) and is returned as a Diagnostic located
// at the innermost expression or statement it came from, with the chain of
// active calls.
//
// Not yet supported: the built-in functions of spec 9 and the file
// statements of spec 10. Using one stops the program with error R000.
class Evaluator {
public:
	struct Options {
		std::uint64_t maxSteps = 0; // --max-steps: stop after this many statements; 0 for no limit (spec 7.8)
	};

	// `environment` must have been made from `program` and is used as is, so
	// it holds the global variables after run().
	Evaluator(const Program& program, Environment& environment, std::istream& input, std::ostream& output,
		Options options);

	// Runs the top-level statements, on a thread with a large stack. Returns
	// the runtime error that stopped the program, if any. Output is flushed
	// either way.
	std::optional<Diagnostic> run();

private:
	// How a statement finished: normally, or by RETURN, which unwinds to the
	// function call.
	enum class Flow { Normal, Return };

	// The storage an expression denotes: a variable, element, field,
	// attribute or pointer target, and how to name it in messages.
	struct Place {
		CellPtr cell;
		std::string text; // e.g. "Names[3]", "Pupil.Name", "P^"
		bool isConstant = false;
	};

	// Something that can be called: a subroutine or method with the frame to
	// run it in, or a built-in function.
	struct Callable {
		const Subroutine* subroutine = nullptr; // null for a built-in, or an implicit constructor
		CallTarget target;
		std::string name;
		bool builtin = false;
	};

	std::optional<Diagnostic> runStatements();

	// Statements
	Flow execute(const Stmt& stmt);
	Flow executeBlock(const Block& block);
	Flow executeNode(const Stmt& stmt);
	void executeAssign(const AssignStmt& node);
	void executeInput(const InputStmt& node);
	void executeOutput(const OutputStmt& node);
	void executeCall(const CallStmt& node);
	Flow executeCase(const CaseStmt& node);
	Flow executeFor(const ForStmt& node);
	Flow executeReturn(const ReturnStmt& node);
	void executeDefine(const DefineStmt& node);

	// Expressions
	Value evaluate(const Expr& expr);
	Value evaluateNode(const Expr& expr);
	Value evaluateBinary(const BinaryExpr& node);
	Value evaluateCall(const CallExpr& node);
	Value evaluateAddressOf(const AddressOfExpr& node);
	Value construct(const NewExpr& node);
	bool condition(const Expr& expr, std::string_view what);
	std::int64_t integer(const Expr& expr, std::string_view what);

	// Places
	std::optional<Place> place(const Expr& expr, bool forWrite);
	Place requirePlace(const Expr& expr, bool forWrite);
	Value containerOf(const Expr& expr, std::string& text);
	Value readPlace(const Expr& expr);

	// Calls
	Callable resolveCallee(const Expr& callee);
	Value invoke(const Callable& callable, const std::vector<ExprPtr>& arguments);
	Argument byReference(const Expr& argument);
	Value callBuiltin(const Callable& callable, const std::vector<ExprPtr>& arguments);

	const Type* resolveType(const TypeSpec& spec);

	// Wraps an error with its location and the current call chain.
	struct LocatedError {
		Diagnostic diagnostic;
	};
	LocatedError located(const RuntimeError& error, SourceLocation location) const;

	const Program& program_;
	Environment& environment_;
	TypeTable& types_;
	std::istream& input_;
	std::ostream& output_;
	Options options_;
	BoundEvaluator evaluateBound_;
	std::uint64_t steps_ = 0;
	std::uintptr_t stackBase_ = 0; // stack position when the program started
	size_t stackLimit_ = 0;         // stack bytes a program may use before R011
	Value returnValue_; // set by RETURN, taken by the function call
	std::map<const TypeSpec*, const Type*> resolvedTypes_;
};
