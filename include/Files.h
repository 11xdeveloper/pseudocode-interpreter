#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Ast.h"
#include "Value.h"

// File handling (spec 10).
//
// A file is known by the name it was opened with. The table of open files
// matches names case-insensitively (spec 13.1, D14), and a file can be open
// in only one mode at a time. Relative names are resolved against the
// program file's folder.
//
// Text files opened for READ are read in full when opened, so EOF is exact.
// Random files are held in memory as numbered slots and written back when
// closed. Each slot stores a value with a description of its type (array
// bounds and record fields included), so GETRECORD can check that it reads
// into a variable of the same type. The on-disk format is private to this
// interpreter (spec 10.2).
//
// File problems are R010; values of the wrong type are R002.
class Files {
public:
	explicit Files(std::filesystem::path baseDirectory);
	~Files();
	Files(const Files&) = delete;
	Files& operator=(const Files&) = delete;

	// Text and random files (spec 10.1, 10.2).
	void open(const std::string& name, FileMode mode, SourceLocation openedAt);
	void close(const std::string& name);

	// Text files.
	std::string readLine(const std::string& name);
	bool atEnd(const std::string& name); // the EOF function
	void writeLine(const std::string& name, const std::string& line);

	// Random files. Addresses are 1-based record numbers. GETRECORD and
	// PUTRECORD do not move the file pointer (spec 13.1, D13).
	void seek(const std::string& name, std::int64_t address);
	void getRecord(const std::string& name, Cell& target, const std::string& targetName);
	void putRecord(const std::string& name, const Cell& source, const std::string& sourceName);

	// A file still open when the program ended (spec 10.1).
	struct LeftOpen {
		std::string name;
		SourceLocation openedAt;
		std::string error; // why writing it back failed, if it did
	};
	// Closes every open file, in the order they were opened.
	std::vector<LeftOpen> closeAll();

private:
	struct OpenFile {
		std::string name; // as written at OPENFILE
		std::filesystem::path path;
		FileMode mode;
		SourceLocation openedAt;
		std::vector<std::string> lines;                 // READ
		size_t nextLine = 0;                            // READ
		std::ofstream stream;                           // WRITE, APPEND
		std::vector<std::optional<std::string>> slots;  // RANDOM: encoded records; empty slots hold nothing
		std::int64_t pointer = 1;                       // RANDOM
	};

	OpenFile& find(const std::string& name, std::initializer_list<FileMode> modes, std::string_view statement);
	std::filesystem::path resolve(const std::string& name) const;
	void finish(OpenFile& file); // flushes or writes back; throws R010 on failure

	std::filesystem::path baseDirectory_;
	std::map<std::string, std::unique_ptr<OpenFile>> files_; // keyed by upper-case name
	std::vector<std::string> openOrder_;                      // keys, oldest first
};
