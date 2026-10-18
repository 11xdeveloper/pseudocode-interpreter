#pragma once

#include <string>
#include <string_view>
#include <vector>

// Errors and warnings from every phase (spec 12).

enum class Phase { Lexical, Syntax, Semantic, Runtime };

enum class Severity { Error, Warning };

struct Diagnostic {
	Severity severity = Severity::Error;
	Phase phase = Phase::Lexical;
	std::string code;               // e.g. "L003"
	int line = 0;
	int column = 0;
	std::string message;
	std::vector<std::string> notes; // extra lines after the caret, e.g. the call chain
};

std::string_view phaseName(Phase phase);

// Formats a diagnostic as in spec 12.1:
//   <file>:<line>:<column>: <Phase> error <Code>: <message>
//      <the source line>
//      <caret under the column>
std::string formatDiagnostic(const Diagnostic& diagnostic, std::string_view fileName, std::string_view source);
