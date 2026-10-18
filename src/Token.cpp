#include "Token.h"

#include <array>
#include <utility>

namespace {

constexpr std::array<std::pair<std::string_view, TokenType>, 66> keywords = { {
	{ "AND", TokenType::And },
	{ "APPEND", TokenType::Append },
	{ "ARRAY", TokenType::Array },
	{ "BOOLEAN", TokenType::Boolean },
	{ "BYREF", TokenType::ByRef },
	{ "BYVAL", TokenType::ByVal },
	{ "CALL", TokenType::Call },
	{ "CASE", TokenType::Case },
	{ "CHAR", TokenType::Char },
	{ "CLASS", TokenType::Class },
	{ "CLOSEFILE", TokenType::CloseFile },
	{ "CONSTANT", TokenType::Constant },
	{ "DATE", TokenType::Date },
	{ "DECLARE", TokenType::Declare },
	{ "DEFINE", TokenType::Define },
	{ "DIV", TokenType::Div },
	{ "ELSE", TokenType::Else },
	{ "ENDCASE", TokenType::EndCase },
	{ "ENDCLASS", TokenType::EndClass },
	{ "ENDFUNCTION", TokenType::EndFunction },
	{ "ENDIF", TokenType::EndIf },
	{ "ENDPROCEDURE", TokenType::EndProcedure },
	{ "ENDTYPE", TokenType::EndType },
	{ "ENDWHILE", TokenType::EndWhile },
	{ "FALSE", TokenType::False },
	{ "FOR", TokenType::For },
	{ "FUNCTION", TokenType::Function },
	{ "GETRECORD", TokenType::GetRecord },
	{ "IF", TokenType::If },
	{ "IN", TokenType::In },
	{ "INHERITS", TokenType::Inherits },
	{ "INPUT", TokenType::Input },
	{ "INTEGER", TokenType::Integer },
	{ "MOD", TokenType::Mod },
	{ "NEW", TokenType::New },
	{ "NEXT", TokenType::Next },
	{ "NOT", TokenType::Not },
	{ "OF", TokenType::Of },
	{ "OPENFILE", TokenType::OpenFile },
	{ "OR", TokenType::Or },
	{ "OTHERWISE", TokenType::Otherwise },
	{ "OUTPUT", TokenType::Output },
	{ "PRIVATE", TokenType::Private },
	{ "PROCEDURE", TokenType::Procedure },
	{ "PUBLIC", TokenType::Public },
	{ "PUTRECORD", TokenType::PutRecord },
	{ "RANDOM", TokenType::Random },
	{ "READ", TokenType::Read },
	{ "READFILE", TokenType::ReadFile },
	{ "REAL", TokenType::Real },
	{ "REPEAT", TokenType::Repeat },
	{ "RETURN", TokenType::Return },
	{ "RETURNS", TokenType::Returns },
	{ "SEEK", TokenType::Seek },
	{ "SET", TokenType::Set },
	{ "STEP", TokenType::Step },
	{ "STRING", TokenType::String },
	{ "SUPER", TokenType::Super },
	{ "THEN", TokenType::Then },
	{ "TO", TokenType::To },
	{ "TRUE", TokenType::True },
	{ "TYPE", TokenType::Type },
	{ "UNTIL", TokenType::Until },
	{ "WHILE", TokenType::While },
	{ "WRITE", TokenType::Write },
	{ "WRITEFILE", TokenType::WriteFile },
} };

}

std::optional<TokenType> lookupKeyword(std::string_view upperCaseWord) {
	for (const auto& [name, type] : keywords) {
		if (name == upperCaseWord) {
			return type;
		}
	}
	return std::nullopt;
}

bool isKeyword(TokenType type) {
	return type >= TokenType::And && type <= TokenType::WriteFile;
}

std::string_view tokenTypeName(TokenType type) {
	if (isKeyword(type)) {
		for (const auto& [name, keywordType] : keywords) {
			if (keywordType == type) {
				return name;
			}
		}
	}
	switch (type) {
	case TokenType::IntegerLiteral: return "integer";
	case TokenType::RealLiteral:    return "real number";
	case TokenType::CharLiteral:    return "character";
	case TokenType::StringLiteral:  return "string";
	case TokenType::DateLiteral:    return "date";
	case TokenType::Identifier:     return "identifier";
	case TokenType::Assign:         return "'←'";
	case TokenType::Plus:           return "'+'";
	case TokenType::Minus:          return "'-'";
	case TokenType::Star:           return "'*'";
	case TokenType::Slash:          return "'/'";
	case TokenType::Ampersand:      return "'&'";
	case TokenType::Equal:          return "'='";
	case TokenType::NotEqual:       return "'<>'";
	case TokenType::Less:           return "'<'";
	case TokenType::Greater:        return "'>'";
	case TokenType::LessEqual:      return "'<='";
	case TokenType::GreaterEqual:   return "'>='";
	case TokenType::Caret:          return "'^'";
	case TokenType::LeftParen:      return "'('";
	case TokenType::RightParen:     return "')'";
	case TokenType::LeftBracket:    return "'['";
	case TokenType::RightBracket:   return "']'";
	case TokenType::Comma:          return "','";
	case TokenType::Colon:          return "':'";
	case TokenType::Dot:            return "'.'";
	case TokenType::Newline:        return "end of line";
	case TokenType::EndOfFile:      return "end of file";
	default:                        return "token";
	}
}
