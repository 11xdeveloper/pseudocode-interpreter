#pragma once

#include <string>

#include "Ast.h"

// Renders the AST as indented text, one statement per line, with expressions
// as fully bracketed prefix forms such as (+ A (* B 2)). Used by --ast and
// for checking the parser's output.
std::string printAst(const Program& program);

std::string printExpr(const Expr* expr);

std::string printLiteral(const Literal& literal);
