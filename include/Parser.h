#pragma once

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"
#include "Token.h"

// Builds the AST from the lexer's tokens (spec section 3).
//
// A recursive-descent parser with one function per grammar rule. Like the
// lexer it never stops at an error: it records a diagnostic, skips to the end
// of the line and carries on, so one run reports every syntax error. Block
// terminators are matched against a stack of open constructs, so a missing
// or wrong END keyword is reported once against the construct it belongs to
// (spec 3.2) instead of causing a cascade of errors.
class Parser {
public:
	explicit Parser(std::vector<Token> tokens);

	Program parse();

	const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
	bool hasErrors() const;

private:
	// Thrown after a syntax error has been reported, to unwind to the nearest
	// point that can skip the rest of the line and carry on.
	struct ParseError {};

	// A construct whose closing keyword has not been reached yet.
	struct OpenConstruct {
		TokenType opener; // If, Case, For, Repeat, While, Procedure, Function, Type or Class
		int line;
		int column;
	};

	// Keeps openStack_ balanced when a parse function returns or throws.
	class OpenScope {
	public:
		OpenScope(Parser& parser, TokenType opener, const Token& at);
		~OpenScope();
		OpenScope(const OpenScope&) = delete;
		OpenScope& operator=(const OpenScope&) = delete;
	private:
		Parser& parser_;
	};

	// How a block ended.
	enum class BodyEnd {
		AtCloser,  // at one of the expected closing keywords (not consumed), or a CASE label line
		Abandoned, // the construct was closed by an error; the caller must not look for its closer
	};

	// Top level and definitions
	std::optional<TypeDef> parseTypeDef();
	Subroutine parseSubroutine(Visibility visibility, bool inClass);
	void parseParameters(std::vector<Parameter>& parameters);
	ClassDef parseClass();
	void parseClassMember(ClassDef& classDef);
	TypeSpec parseTypeSpec(bool allowOpenArray);

	// Blocks and statements
	BodyEnd parseBody(Block& into, std::initializer_list<TokenType> closers, bool inCase = false);
	StmtPtr parseStatementSafely();
	StmtPtr parseStatement();
	DeclareStmt parseDeclare();
	StmtPtr parseConstant();
	StmtPtr parseExpressionStatement();
	StmtPtr parseInput();
	StmtPtr parseOutput();
	StmtPtr parseCall();
	StmtPtr parseIf(bool chainedElseIf);
	StmtPtr parseCase();
	BodyEnd parseClauseBody(Block& into);
	CaseLabel parseCaseLabel();
	ExprPtr parseLabelValue();
	StmtPtr parseFor();
	StmtPtr parseRepeat();
	StmtPtr parseWhile();
	StmtPtr parseReturn();
	StmtPtr parseFileStatement();
	StmtPtr parseDefine();

	// Expressions, lowest to highest precedence (spec 3.1, 6.1)
	ExprPtr parseExpression();
	ExprPtr parseOr();
	ExprPtr parseAnd();
	ExprPtr parseNot();
	ExprPtr parseComparison();
	ExprPtr parseConcat();
	ExprPtr parseAdditive();
	ExprPtr parseTerm();
	ExprPtr parseUnary();
	ExprPtr parsePostfix();
	ExprPtr parsePrimary();
	std::vector<ExprPtr> parseArguments();
	ExprPtr parseLValue(std::string_view purpose);
	Literal parseLiteralValue(std::string_view expected);
	Identifier parseMemberName(bool allowNew);

	// Token access
	const Token& peek(size_t ahead = 0) const;
	const Token& current() const { return peek(); }
	bool check(TokenType type) const { return current().type == type; }
	bool match(TokenType type);
	const Token& advance();
	const Token& expect(TokenType type, std::string_view expected);
	Identifier expectIdentifier(std::string_view expected);
	void expectEndOfLine();
	void skipNewlines();
	bool atLineStart() const;
	bool isCaseLabelLine() const;

	// Error handling and recovery
	void error(const char* code, int line, int column, std::string message);
	void error(const char* code, const Token& at, std::string message);
	[[noreturn]] void fail(const Token& at, std::string message);
	[[noreturn]] void failExpected(std::string_view expected, std::string_view hint = {});
	void synchronize();
	void skipLine();
	template <class ParseHeader>
	void recoverLine(ParseHeader&& parseHeader);
	bool handleStrayTerminator(bool wrongCloserEndsConstruct);
	void reportUnclosed(const OpenConstruct& open);
	void reportDefinitionInside(const Token& token);
	bool insideDefinition() const;

	std::vector<Token> tokens_;
	size_t pos_ = 0;
	std::vector<OpenConstruct> openStack_;
	// Set after "<-" is re-read as "<": the next operand gets a unary minus (spec 2.4).
	bool negateNextOperand_ = false;
	int lastErrorLine_ = 0;
	std::vector<Diagnostic> diagnostics_;
};

// Parses a construct's header line. If it fails, the error is already
// reported, so skip the rest of the line and let the caller go on to parse
// the body. This keeps the body and its closing keyword matched up.
template <class ParseHeader>
void Parser::recoverLine(ParseHeader&& parseHeader) {
	try {
		parseHeader();
	}
	catch (const ParseError&) {
		synchronize();
	}
}
