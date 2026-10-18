#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include "Analyzer.h"
#include "AstPrinter.h"
#include "Environment.h"
#include "Evaluator.h"
#include "Lexer.h"
#include "Parser.h"
#include "Types.h"

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

constexpr const char* Usage =
	"Usage: PseudocodeInterpreter [options] [file]\n"
	"Runs a 9618 pseudocode program (test.txt if no file is given).\n"
	"\n"
	"  --strict        every variable must be declared\n"
	"  --defaults      variables start as 0, 0.0, \"\", FALSE and so on\n"
	"  --max-steps N   stop after N statements (catches endless loops)\n"
	"  --max-depth N   allow calls to nest N deep (default 10000)\n"
	"  --seed N        make RAND give the same numbers on every run\n"
	"  --tokens        print the tokens instead of running\n"
	"  --ast           print the syntax tree instead of running\n"
	"  --types         print the defined types instead of running\n";

bool parseCount(std::string_view text, std::uint64_t& out) {
	auto result = std::from_chars(text.data(), text.data() + text.size(), out);
	return result.ec == std::errc() && result.ptr == text.data() + text.size() && out > 0;
}

// Prints diagnostics in source order. Returns true if any is an error.
bool report(std::vector<Diagnostic> diagnostics, std::string_view fileName, std::string_view source) {
	std::stable_sort(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& a, const Diagnostic& b) {
		return a.line != b.line ? a.line < b.line : a.column < b.column;
	});
	bool hasErrors = false;
	for (const Diagnostic& diagnostic : diagnostics) {
		std::cerr << formatDiagnostic(diagnostic, fileName, source);
		hasErrors = hasErrors || diagnostic.severity == Severity::Error;
	}
	return hasErrors;
}

}

// Exit status (spec 12.3): 0 on success, 1 for lexical, syntax or semantic
// errors, 2 for runtime errors.
int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	std::ios::sync_with_stdio(false);

	enum class Mode { Run, Tokens, Ast, Types };
	Mode mode = Mode::Run;
	Environment::Options environmentOptions;
	Evaluator::Options evaluatorOptions;
	std::string fileName = "test.txt";

	for (int i = 1; i < argc; ++i) {
		std::string_view arg = argv[i];
		if (arg == "--tokens") mode = Mode::Tokens;
		else if (arg == "--ast") mode = Mode::Ast;
		else if (arg == "--types") mode = Mode::Types;
		else if (arg == "--strict") environmentOptions.strict = true;
		else if (arg == "--defaults") environmentOptions.useDefaults = true;
		else if (arg == "--help") {
			std::cout << Usage;
			return 0;
		}
		else if (arg == "--seed") {
			std::uint64_t seed = 0;
			std::string_view text = i + 1 < argc ? argv[i + 1] : "";
			auto result = std::from_chars(text.data(), text.data() + text.size(), seed);
			if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size()) {
				std::cerr << "--seed needs a whole number, as in --seed 42\n";
				return 1;
			}
			++i;
			evaluatorOptions.seed = seed;
		}
		else if (arg == "--max-steps" || arg == "--max-depth") {
			std::uint64_t count = 0;
			if (i + 1 >= argc || !parseCount(argv[i + 1], count)) {
				std::cerr << arg << " needs a positive whole number, as in " << arg << " 1000\n";
				return 1;
			}
			++i;
			if (arg == "--max-steps") evaluatorOptions.maxSteps = count;
			else environmentOptions.maxDepth = static_cast<size_t>(count);
		}
		else if (arg.starts_with("--")) {
			std::cerr << "Unknown option " << arg << "\n" << Usage;
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
	if (mode == Mode::Tokens) {
		printTokens(tokens);
		return report(diagnostics, fileName, source) ? 1 : 0;
	}

	Parser parser(std::move(tokens));
	Program program = parser.parse();
	diagnostics.insert(diagnostics.end(), parser.diagnostics().begin(), parser.diagnostics().end());
	if (lexer.hasErrors() || parser.hasErrors()) {
		if (mode == Mode::Ast) {
			std::cout << printAst(program);
		}
		report(diagnostics, fileName, source);
		return 1;
	}

	TypeTable types;
	types.build(program);
	Environment environment(program, types, environmentOptions);
	// The semantic phase (spec 12): everything that can be checked before running.
	Analyzer analyzer(program, types, environment, { environmentOptions.strict });
	analyzer.analyze();
	// Read last: resolving signatures and declarations adds to the type table's diagnostics.
	diagnostics.insert(diagnostics.end(), types.diagnostics().begin(), types.diagnostics().end());
	diagnostics.insert(diagnostics.end(), environment.diagnostics().begin(), environment.diagnostics().end());
	diagnostics.insert(diagnostics.end(), analyzer.diagnostics().begin(), analyzer.diagnostics().end());

	if (mode == Mode::Ast || mode == Mode::Types) {
		std::cout << (mode == Mode::Ast ? printAst(program) : types.dump());
		return report(diagnostics, fileName, source) ? 1 : 0;
	}
	// A program with errors found before running produces no output (spec 12).
	if (report(diagnostics, fileName, source)) {
		return 1;
	}

	// Spec 10: file names are relative to the program's folder.
	evaluatorOptions.baseDirectory = std::filesystem::path(std::u8string(fileName.begin(), fileName.end())).parent_path();
	Evaluator evaluator(program, environment, std::cin, std::cout, evaluatorOptions);
	try {
		std::optional<Diagnostic> error = evaluator.run();
		if (error) {
			std::cerr << formatDiagnostic(*error, fileName, source);
		}
		for (const Diagnostic& warning : evaluator.warnings()) {
			std::cerr << formatDiagnostic(warning, fileName, source);
		}
		if (error) {
			return 2;
		}
	}
	catch (const std::exception& error) {
		std::cout.flush();
		std::cerr << fileName << ": Internal error in the interpreter: " << error.what() << "\n";
		return 2;
	}
	return 0;
}
