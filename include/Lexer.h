#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostic.h"
#include "Token.h"

// Converts UTF-8 source text into tokens (spec section 2).
//
// The lexer never stops at an error: it records a diagnostic, emits the most
// likely intended token and carries on, so one run reports every lexical error.
// The token stream always ends with Newline (if non-empty) and EndOfFile.
class Lexer {
public:
	explicit Lexer(std::string_view source);

	std::vector<Token> tokenize();

	const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
	bool hasErrors() const;

private:
	struct CodePoint {
		char32_t value;
		int length; // bytes; 1 for an invalid byte
		bool valid;
	};

	bool atEnd() const { return pos_ >= source_.size(); }
	unsigned char peek(size_t ahead = 0) const;
	CodePoint decodeAt(size_t pos) const;
	void advance();
	void advanceBytes(size_t count);

	void lexNumber();
	bool tryLexDate();
	void lexLeadingDotNumber();
	void lexWord();
	void lexAsciiSymbol();
	void lexNonAscii();
	void lexCharLiteral(int openerLength);
	void lexStringLiteral(int openerLength);
	void skipComment();

	void addToken(TokenType type, size_t start, int line, int column, LiteralValue value = {});
	void emit(Token token);
	void error(const char* code, int line, int column, std::string message);

	std::string_view source_;
	size_t pos_ = 0;
	int line_ = 1;
	int column_ = 1;
	int bracketDepth_ = 0;
	std::vector<Token> tokens_;
	std::vector<Diagnostic> diagnostics_;
};
