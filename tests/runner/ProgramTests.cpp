// Runs pseudocode programs through the interpreter and compares what they
// do with what they should do.
//
// Usage: program_tests --interpreter PATH [--update] [--keep] FILE_OR_FOLDER...
//
// Each test is a NAME.pseudo program. Comment lines at its top may hold:
//   // ARGS: --seed 42          options passed before the program file
//   // STDIN: some text         a line of standard input (repeatable)
//   // STDIN-CRLF: some text    the same, ending with \r\n
// A folder NAME.files next to it is copied into the folder the program runs
// in, for programs that read files.
//
// NAME.expected holds the exit status, standard output and standard error:
//   exit: 2
//   --- stdout
//   ...
//   --- stderr
//   ...
//
// Each program runs in a new temporary folder, under its own file name, so
// diagnostics read "NAME.pseudo:3:1: ..." wherever the test is run.
// --update writes the actual results to the .expected files instead of
// comparing, so intended changes can be reviewed as a diff. --keep leaves
// the temporary folders in place and prints where they are.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Options {
	fs::path interpreter;
	bool update = false;
	bool keep = false;
	std::vector<fs::path> tests;
};

struct Directives {
	std::vector<std::string> arguments;
	std::string input;
};

std::string readFile(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream content;
	content << in.rdbuf();
	return content.str();
}

void writeFile(const fs::path& path, const std::string& content) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << content;
}

std::string normaliseLineEndings(std::string text) {
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
			continue;
		}
		result += text[i];
	}
	return result;
}

// The directives in the comment lines at the top of a program.
Directives readDirectives(const std::string& source) {
	Directives directives;
	std::istringstream lines(source);
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (!line.starts_with("//")) {
			break;
		}
		std::string text = line.substr(2);
		size_t start = text.find_first_not_of(' ');
		text = start == std::string::npos ? "" : text.substr(start);

		auto value = [&](std::string_view name) -> std::optional<std::string> {
			if (!text.starts_with(name)) {
				return std::nullopt;
			}
			std::string rest = text.substr(name.size());
			return rest.starts_with(' ') ? rest.substr(1) : rest;
		};
		if (auto arguments = value("ARGS:")) {
			std::istringstream words(*arguments);
			std::string word;
			while (words >> word) {
				directives.arguments.push_back(word);
			}
		}
		else if (auto input = value("STDIN-CRLF:")) {
			directives.input += *input + "\r\n";
		}
		else if (auto input = value("STDIN:")) {
			directives.input += *input + "\n";
		}
	}
	return directives;
}

std::string quote(const fs::path& path) {
#ifdef _WIN32
	return "\"" + path.string() + "\"";
#else
	std::string text = path.string();
	std::string result = "'";
	for (char c : text) {
		result += c == '\'' ? std::string("'\\''") : std::string(1, c);
	}
	return result + "'";
#endif
}

fs::path makeTemporaryFolder() {
	std::random_device device;
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	fs::path folder = fs::temp_directory_path()
		/ ("pseudocode-test-" + std::to_string(stamp) + "-" + std::to_string(device()));
	fs::create_directories(folder / "work");
	fs::create_directories(folder / "io");
	return folder;
}

// Runs one program and returns its results in the .expected format.
std::string run(const Options& options, const fs::path& program, fs::path& folder) {
	std::string source = readFile(program);
	Directives directives = readDirectives(source);

	folder = makeTemporaryFolder();
	fs::path work = folder / "work";
	fs::path io = folder / "io";
	fs::copy_file(program, work / program.filename());
	fs::path fixtures = program;
	fixtures.replace_extension(".files");
	if (fs::is_directory(fixtures)) {
		fs::copy(fixtures, work, fs::copy_options::recursive);
	}
	writeFile(io / "stdin", directives.input);

	std::string command = "cd " + quote(work) + " && " + quote(options.interpreter);
#ifdef _WIN32
	command = "cd /d " + quote(work) + " && " + quote(options.interpreter);
#endif
	for (const std::string& argument : directives.arguments) {
		command += " " + argument;
	}
	command += " " + quote(program.filename()) + " < " + quote(io / "stdin") + " > " + quote(io / "stdout")
		+ " 2> " + quote(io / "stderr");
#ifdef _WIN32
	command = "\"" + command + "\""; // cmd.exe strips one pair of outer quotes
#endif

	int status = std::system(command.c_str());
	std::string exit;
#ifdef _WIN32
	exit = std::to_string(status);
#else
	if (WIFEXITED(status)) {
		exit = std::to_string(WEXITSTATUS(status));
	}
	else if (WIFSIGNALED(status)) {
		exit = "crashed with signal " + std::to_string(WTERMSIG(status));
	}
	else {
		exit = "unknown status " + std::to_string(status);
	}
#endif

	auto section = [](std::string text) {
		text = normaliseLineEndings(std::move(text));
		if (!text.empty() && text.back() != '\n') {
			text += "\n[no newline at end]\n";
		}
		return text;
	};
	return "exit: " + exit + "\n--- stdout\n" + section(readFile(io / "stdout"))
		+ "--- stderr\n" + section(readFile(io / "stderr"));
}

std::vector<std::string> splitLines(const std::string& text) {
	std::vector<std::string> lines;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		lines.push_back(line);
	}
	return lines;
}

// Shows where the actual results first differ from the expected ones.
void showDifference(const std::string& expected, const std::string& actual) {
	std::vector<std::string> want = splitLines(expected);
	std::vector<std::string> got = splitLines(actual);
	size_t i = 0;
	while (i < want.size() && i < got.size() && want[i] == got[i]) {
		++i;
	}
	std::cout << "  first difference at line " << i + 1 << " of the results:\n";
	std::cout << "  expected: " << (i < want.size() ? want[i] : "(end)") << "\n";
	std::cout << "  actual:   " << (i < got.size() ? got[i] : "(end)") << "\n";
	std::cout << "  ---- actual results ----\n";
	for (const std::string& line : got) {
		std::cout << "  | " << line << "\n";
	}
}

void collect(const fs::path& path, std::vector<fs::path>& programs) {
	if (fs::is_directory(path)) {
		std::vector<fs::path> found;
		for (const auto& entry : fs::recursive_directory_iterator(path)) {
			if (entry.is_regular_file() && entry.path().extension() == ".pseudo") {
				found.push_back(entry.path());
			}
		}
		std::sort(found.begin(), found.end());
		programs.insert(programs.end(), found.begin(), found.end());
	}
	else {
		programs.push_back(path);
	}
}

}

int main(int argc, char* argv[]) {
	Options options;
	for (int i = 1; i < argc; ++i) {
		std::string argument = argv[i];
		if (argument == "--interpreter" && i + 1 < argc) {
			options.interpreter = fs::absolute(argv[++i]);
		}
		else if (argument == "--update") {
			options.update = true;
		}
		else if (argument == "--keep") {
			options.keep = true;
		}
		else {
			options.tests.push_back(fs::absolute(argument));
		}
	}
	if (options.interpreter.empty() || options.tests.empty()) {
		std::cerr << "Usage: program_tests --interpreter PATH [--update] [--keep] FILE_OR_FOLDER...\n";
		return 2;
	}

	std::vector<fs::path> programs;
	for (const fs::path& test : options.tests) {
		collect(test, programs);
	}

	int failed = 0;
	for (const fs::path& program : programs) {
		fs::path expectedFile = program;
		expectedFile.replace_extension(".expected");
		fs::path folder;
		std::string actual = run(options, program, folder);

		if (options.update) {
			bool changed = !fs::exists(expectedFile) || readFile(expectedFile) != actual;
			writeFile(expectedFile, actual);
			std::cout << (changed ? "UPDATED " : "same    ") << program.filename().string() << "\n";
		}
		else if (!fs::exists(expectedFile)) {
			++failed;
			std::cout << "FAIL " << program.filename().string() << ": no " << expectedFile.filename().string()
				<< " (run with --update to create it)\n";
			showDifference("", actual);
		}
		else {
			std::string expected = normaliseLineEndings(readFile(expectedFile));
			if (expected == actual) {
				std::cout << "PASS " << program.filename().string() << "\n";
			}
			else {
				++failed;
				std::cout << "FAIL " << program.filename().string() << "\n";
				showDifference(expected, actual);
			}
		}

		if (options.keep) {
			std::cout << "  ran in " << folder.string() << "\n";
		}
		else {
			std::error_code ignored;
			fs::remove_all(folder, ignored);
		}
	}

	if (!options.update) {
		std::cout << programs.size() << " programs, " << failed << " failed\n";
	}
	return failed == 0 ? 0 : 1;
}
