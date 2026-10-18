#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

#include "Lexer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

std::string describeValue(const LiteralValue& value) {
	struct Visitor {
		std::string operator()(std::monostate) const { return ""; }
		std::string operator()(std::int64_t v) const { return std::to_string(v); }
		std::string operator()(double v) const { std::ostringstream out; out << v; return out.str(); }
		std::string operator()(char v) const { return std::string("'") + v + "'"; }
		std::string operator()(const std::string& v) const { return "\"" + v + "\""; }
		std::string operator()(const Date& v) const {
			return std::to_string(v.day) + "/" + std::to_string(v.month) + "/" + std::to_string(v.year);
		}
	};
	return std::visit(Visitor{}, value);
}

}

// For now this only runs the lexer on test.txt and prints the token stream.
int main() {
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	std::string fileName = "test.txt";
	std::ifstream file(fileName, std::ios::binary);
	if (!file) {
		std::cerr << "Cannot open " << fileName << "\n";
		return 1;
	}
	std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	Lexer lexer(source);
	std::vector<Token> tokens = lexer.tokenize();

	for (const Token& token : tokens) {
		std::cout << token.line << ':' << token.column << '\t' << tokenTypeName(token.type);
		if (token.type == TokenType::Identifier) {
			std::cout << ' ' << token.lexeme;
		}
		std::string value = describeValue(token.value);
		if (!value.empty()) {
			std::cout << ' ' << value;
		}
		std::cout << '\n';
	}

	for (const Diagnostic& diagnostic : lexer.diagnostics()) {
		std::cerr << formatDiagnostic(diagnostic, fileName, source);
	}
	return lexer.hasErrors() ? 1 : 0;
}
