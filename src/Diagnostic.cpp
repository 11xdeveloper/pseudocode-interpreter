#include "Diagnostic.h"

namespace {

// Returns the text of a 1-based line without its line ending, or false if the
// source has fewer lines.
bool findLine(std::string_view source, int lineNumber, std::string_view& out) {
	size_t start = 0;
	for (int line = 1; line < lineNumber; ++line) {
		size_t end = source.find_first_of("\r\n", start);
		if (end == std::string_view::npos) {
			return false;
		}
		start = end + ((source[end] == '\r' && end + 1 < source.size() && source[end + 1] == '\n') ? 2 : 1);
	}
	if (start > source.size()) {
		return false;
	}
	size_t end = source.find_first_of("\r\n", start);
	out = source.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
	return true;
}

}

std::string_view phaseName(Phase phase) {
	switch (phase) {
	case Phase::Lexical:  return "Lexical";
	case Phase::Syntax:   return "Syntax";
	case Phase::Semantic: return "Semantic";
	case Phase::Runtime:  return "Runtime";
	}
	return "Unknown";
}

std::string formatDiagnostic(const Diagnostic& diagnostic, std::string_view fileName, std::string_view source) {
	std::string out;
	out += fileName;
	out += ':' + std::to_string(diagnostic.line) + ':' + std::to_string(diagnostic.column) + ": ";
	out += phaseName(diagnostic.phase);
	out += diagnostic.severity == Severity::Error ? " error " : " warning ";
	out += diagnostic.code + ": " + diagnostic.message + '\n';

	std::string_view lineText;
	if (diagnostic.line > 0 && findLine(source, diagnostic.line, lineText)) {
		out += "   ";
		out += lineText;
		out += "\n   ";
		// Columns count code points, so skip UTF-8 continuation bytes. Tabs are
		// copied so the caret lines up however the terminal renders them.
		int column = 1;
		for (size_t i = 0; i < lineText.size() && column < diagnostic.column; ++i) {
			unsigned char c = static_cast<unsigned char>(lineText[i]);
			if ((c & 0xC0) == 0x80) {
				continue;
			}
			out += c == '\t' ? '\t' : ' ';
			++column;
		}
		out += "^\n";
	}

	for (const std::string& note : diagnostic.notes) {
		out += "   " + note + '\n';
	}
	return out;
}
