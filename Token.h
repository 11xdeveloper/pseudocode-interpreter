#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "Date.h"

enum class TokenType {
	// Literals and names
	IntegerLiteral, RealLiteral, CharLiteral, StringLiteral, DateLiteral, Identifier,

	// Keywords (spec 2.1)
	And, Append, Array, Boolean, ByRef, ByVal, Call, Case, Char, Class, CloseFile,
	Constant, Date, Declare, Define, Div, Else, EndCase, EndClass, EndFunction,
	EndIf, EndProcedure, EndType, EndWhile, False, For, Function, GetRecord, If,
	In, Inherits, Input, Integer, Mod, New, Next, Not, Of, OpenFile, Or, Otherwise, Output,
	Private, Procedure, Public, PutRecord, Random, Read, ReadFile, Real, Repeat,
	Return, Returns, Seek, Set, Step, String, Super, Then, To, True, Type, Until,
	While, Write, WriteFile,

	// Operators and punctuation (spec 2.4)
	Assign,                                    // <- or the arrow character
	Plus, Minus, Star, Slash, Ampersand,
	Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
	Caret,
	LeftParen, RightParen, LeftBracket, RightBracket,
	Comma, Colon, Dot,

	Newline,
	EndOfFile,
};

// Value of a literal token. TRUE and FALSE are keyword tokens and carry no value.
using LiteralValue = std::variant<std::monostate, std::int64_t, double, char, std::string, Date>;

struct Token {
	TokenType type = TokenType::EndOfFile;
	// The text exactly as written in the source, including quotes for literals.
	// The parser uses this to tell an Assign written as "<-" apart from the arrow
	// (spec 2.4: "X<-1" may need re-reading as "<" followed by unary "-").
	std::string lexeme;
	int line = 0;   // 1-based
	int column = 0; // 1-based, counted in code points
	LiteralValue value;
};

// Looks up an upper-case word in the keyword table.
std::optional<TokenType> lookupKeyword(std::string_view upperCaseWord);

bool isKeyword(TokenType type);

// A readable name for error messages: "ENDIF", "'('", "identifier", "end of line".
std::string_view tokenTypeName(TokenType type);
