#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Environment.h"
#include "Lexer.h"
#include "Parser.h"
#include "RuntimeError.h"
#include "Types.h"
#include "Value.h"

// Shared helpers for the unit tests.

// A program parsed, with its types and subroutines resolved.
struct Compiled {
	Program program;
	TypeTable types;
	std::unique_ptr<Environment> environment;
	bool clean = false; // no lexical, syntax or definition errors

	const DeclareStmt& declare(size_t statement) const {
		return std::get<DeclareStmt>(program.statements[statement]->node);
	}
};

inline std::unique_ptr<Compiled> compile(const std::string& source, Environment::Options options = {}) {
	auto compiled = std::make_unique<Compiled>();
	Lexer lexer(source);
	Parser parser(lexer.tokenize());
	compiled->program = parser.parse();
	compiled->types.build(compiled->program);
	compiled->environment = std::make_unique<Environment>(compiled->program, compiled->types, options);
	compiled->clean = !lexer.hasErrors() && !parser.hasErrors() && !compiled->types.hasErrors()
		&& compiled->environment->diagnostics().empty();
	return compiled;
}

// "INTEGER:5", "REAL:2.5", or "<R001>" for a value that cannot be shown.
inline std::string describe(const Value& value) {
	try {
		return typeName(typeOf(value)) + ":" + formatValue(value);
	}
	catch (const RuntimeError& error) {
		return "<" + error.code() + ">";
	}
}

inline Value I(std::int64_t value) { return value; }
inline Value R(double value) { return value; }
inline Value S(const char* value) { return std::string(value); }
inline Value C(char value) { return value; }
inline Value B(bool value) { return value; }

// Evaluates array bounds that are INTEGER literals, possibly negated.
inline std::int64_t literalBound(const Expr& expr) {
	if (const auto* literal = std::get_if<LiteralExpr>(&expr.node)) {
		return std::get<std::int64_t>(literal->value);
	}
	if (const auto* unary = std::get_if<UnaryExpr>(&expr.node)) {
		return -literalBound(*unary->operand);
	}
	throw RuntimeError("R002", "the test bound evaluator only handles INTEGER literals");
}
