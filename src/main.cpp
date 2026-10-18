#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include "AstPrinter.h"
#include "Lexer.h"
#include "Parser.h"

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

void printTokens(const std::vector<Token>& tokens) {
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
}

}

// Usage: PseudocodeInterpreter [--tokens | --ast] [file]
// For now this lexes and parses the file (test.txt by default) and prints the
// AST, or with --tokens the token stream.
int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	bool showTokens = false;
	std::string fileName = "test.txt";
	for (int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];
		if (arg == "--tokens") {
			showTokens = true;
		}
		else if (arg == "--ast") {
			showTokens = false;
		}
		else if (arg.starts_with("--")) {
			std::cerr << "Unknown option " << arg << "\nUsage: PseudocodeInterpreter [--tokens | --ast] [file]\n";
			return 1;
		}
		else {
			fileName = arg;
		}
	}

	std::ifstream file(fileName, std::ios::binary);
	if (!file) {
		std::cerr << "Cannot open " << fileName << "\n";
		return 1;
	}
	std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	Lexer lexer(source);
	std::vector<Token> tokens = lexer.tokenize();
	std::vector<Diagnostic> diagnostics = lexer.diagnostics();

	if (showTokens) {
		printTokens(tokens);
	}
	else {
		Parser parser(std::move(tokens));
		Program program = parser.parse();
		diagnostics.insert(diagnostics.end(), parser.diagnostics().begin(), parser.diagnostics().end());
		std::cout << printAst(program);
	}

	std::stable_sort(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& a, const Diagnostic& b) {
		return a.line != b.line ? a.line < b.line : a.column < b.column;
	});
	bool hasErrors = false;
	for (const Diagnostic& diagnostic : diagnostics) {
		std::cerr << formatDiagnostic(diagnostic, fileName, source);
		hasErrors = hasErrors || diagnostic.severity == Severity::Error;
	}
	return hasErrors ? 1 : 0;
}
