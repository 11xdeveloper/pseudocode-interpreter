#include "Files.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <system_error>

#include "RuntimeError.h"

namespace {

// Spec 10.2 leaves the random-file format to the implementation. It is:
//   the header line below, the number of slots on a line, then per slot
//   either "-" (empty) or the payload's length in bytes on a line, followed
//   by the payload and a newline.
// A payload is the stored variable's type name, a newline, a signature
// describing its exact type, a newline, then its encoded value.
constexpr std::string_view RandomHeader = "PSEUDOCODE RANDOM FILE 1\n";

// Writing a record far past the end makes empty slots up to it; this keeps a
// mistyped address from using up memory.
constexpr std::int64_t MaxRecordAddress = 1'000'000;

std::string toUpper(std::string_view text) {
	std::string result(text);
	for (char& c : result) {
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}
	return result;
}

std::string modeName(FileMode mode) {
	switch (mode) {
	case FileMode::Read:   return "READ";
	case FileMode::Write:  return "WRITE";
	case FileMode::Append: return "APPEND";
	case FileMode::Random: return "RANDOM";
	}
	return "?";
}

std::string inQuotes(const std::string& name) {
	return "\"" + name + "\"";
}

[[noreturn]] void fileError(const std::string& message) {
	throw RuntimeError("R010", message);
}

// Spec 10.1: lines end with \n or \r\n; a final line ending is optional.
std::vector<std::string> splitLines(const std::string& content) {
	std::vector<std::string> lines;
	size_t start = 0;
	while (start < content.size()) {
		size_t end = content.find('\n', start);
		if (end == std::string::npos) {
			end = content.size();
		}
		std::string line = content.substr(start, end - start);
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lines.push_back(std::move(line));
		start = end + 1;
	}
	return lines;
}

std::optional<std::string> readWholeFile(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return std::nullopt;
	}
	std::ostringstream content;
	content << in.rdbuf();
	return content.str();
}

// ---------- random file records ----------

std::string typeSignature(const Type* type) {
	if (!type) {
		return "?";
	}
	switch (type->kind) {
	case TypeKind::Enum: {
		std::string text = "ENUM " + type->name + "(";
		for (size_t i = 0; i < type->enumValues.size(); ++i) {
			text += (i > 0 ? "," : "") + type->enumValues[i];
		}
		return text + ")";
	}
	case TypeKind::Set:
		return "SET " + type->name + " OF " + typeSignature(type->element);
	default:
		return type->name;
	}
}

// The exact type of what a cell holds, with array bounds and record fields,
// so a record is only read back into a variable of the same shape.
std::string signature(const Cell& cell) {
	if (const auto* array = std::get_if<ArrayRef>(&cell.value); array && *array) {
		const ArrayValue& value = **array;
		return "ARRAY" + value.boundsText() + " OF "
			+ (value.elements.empty() ? typeSignature(value.type->element) : signature(*value.elements.front()));
	}
	if (const auto* record = std::get_if<RecordRef>(&cell.value); record && *record) {
		const RecordValue& value = **record;
		std::string text = "RECORD " + value.type->name + "{";
		for (size_t i = 0; i < value.fields.size(); ++i) {
			text += value.type->fields[i].name + ":" + signature(*value.fields[i]) + ";";
		}
		return text + "}";
	}
	return typeSignature(cell.type ? cell.type : typeOf(cell.value));
}

bool isAtomicSignature(std::string_view signature) {
	return signature == "INTEGER" || signature == "REAL" || signature == "CHAR"
		|| signature == "STRING" || signature == "BOOLEAN" || signature == "DATE";
}

void encode(const Value& value, std::string& out) {
	std::visit(Overloaded{
		[&](std::monostate) { out += 'U'; },
		[&](std::int64_t v) { out += 'I' + std::to_string(v) + ';'; },
		[&](double v) {
			char buffer[64];
			auto result = std::to_chars(buffer, buffer + sizeof buffer, v);
			out += 'R';
			out.append(buffer, result.ptr);
			out += ';';
		},
		[&](char v) { out += 'C' + std::to_string(static_cast<unsigned char>(v)) + ';'; },
		[&](const std::string& v) { out += 'S' + std::to_string(v.size()) + ':' + v; },
		[&](bool v) { out += v ? "B1;" : "B0;"; },
		[&](const Date& v) {
			out += 'D' + std::to_string(v.year) + '/' + std::to_string(v.month) + '/' + std::to_string(v.day) + ';';
		},
		[&](const EnumValue& v) { out += 'E' + std::to_string(v.ordinal) + ';'; },
		[&](const PointerValue&) {
			throw RuntimeError("R002", "A pointer cannot be stored in a file: the address it holds would mean nothing when read back");
		},
		[&](const ArrayRef& v) {
			out += 'A' + std::to_string(v->elements.size()) + ':';
			for (const CellPtr& element : v->elements) {
				encode(element->value, out);
			}
		},
		[&](const RecordRef& v) {
			out += 'T' + std::to_string(v->fields.size()) + ':';
			for (const CellPtr& field : v->fields) {
				encode(field->value, out);
			}
		},
		[&](const ObjectRef&) {
			throw RuntimeError("R002", "An object cannot be stored in a random file. Store its attributes in a record instead");
		},
		[&](const SetRef& v) {
			out += 'Z' + std::to_string(v->elements.size()) + ':';
			for (const Value& element : v->elements) {
				encode(element, out);
			}
		},
	}, value);
}

// Reads an encoded value back into a cell of the same shape, which the
// signature check has already confirmed.
class Decoder {
public:
	Decoder(std::string_view data, std::string damagedMessage) : data_(data), damaged_(std::move(damagedMessage)) {}

	bool done() const { return pos_ == data_.size(); }

	void into(Cell& cell) {
		char tag = next();
		switch (tag) {
		case 'U':
			cell.value = Value{};
			return;
		case 'A': {
			size_t count = static_cast<size_t>(number(':'));
			auto* array = std::get_if<ArrayRef>(&cell.value);
			if (!array || !*array || (*array)->elements.size() != count) damaged();
			for (const CellPtr& element : (*array)->elements) {
				into(*element);
			}
			return;
		}
		case 'T': {
			size_t count = static_cast<size_t>(number(':'));
			auto* record = std::get_if<RecordRef>(&cell.value);
			if (!record || !*record || (*record)->fields.size() != count) damaged();
			for (const CellPtr& field : (*record)->fields) {
				into(*field);
			}
			return;
		}
		case 'Z': {
			if (!cell.type || cell.type->kind != TypeKind::Set) damaged();
			std::int64_t count = number(':');
			auto set = std::make_shared<SetValue>();
			set->type = cell.type;
			for (std::int64_t i = 0; i < count; ++i) {
				set->elements.push_back(atom(next(), cell.type->element));
			}
			cell.value = SetRef(std::move(set));
			return;
		}
		default:
			storeValue(cell, atom(tag, cell.type));
		}
	}

private:
	[[noreturn]] void damaged() const {
		fileError(damaged_);
	}

	char next() {
		if (pos_ >= data_.size()) damaged();
		return data_[pos_++];
	}

	std::string_view until(char end) {
		size_t found = data_.find(end, pos_);
		if (found == std::string_view::npos) damaged();
		std::string_view text = data_.substr(pos_, found - pos_);
		pos_ = found + 1;
		return text;
	}

	std::int64_t number(char end) {
		std::string_view text = until(end);
		std::int64_t value = 0;
		auto result = std::from_chars(text.data(), text.data() + text.size(), value);
		if (result.ec != std::errc() || result.ptr != text.data() + text.size()) damaged();
		return value;
	}

	Value atom(char tag, const Type* type) {
		switch (tag) {
		case 'I':
			return number(';');
		case 'R': {
			std::string_view text = until(';');
			double value = 0.0;
			auto result = std::from_chars(text.data(), text.data() + text.size(), value);
			if (result.ec != std::errc() || result.ptr != text.data() + text.size()) damaged();
			return value;
		}
		case 'C': {
			std::int64_t code = number(';');
			if (code < 0 || code > 255) damaged();
			return static_cast<char>(static_cast<unsigned char>(code));
		}
		case 'S': {
			std::int64_t length = number(':');
			if (length < 0 || static_cast<size_t>(length) > data_.size() - pos_) damaged();
			std::string text(data_.substr(pos_, static_cast<size_t>(length)));
			pos_ += static_cast<size_t>(length);
			return text;
		}
		case 'B': {
			std::string_view text = until(';');
			if (text != "0" && text != "1") damaged();
			return text == "1";
		}
		case 'D': {
			std::int64_t year = number('/');
			std::int64_t month = number('/');
			std::int64_t day = number(';');
			if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31
				|| !isValidDate(static_cast<int>(day), static_cast<int>(month), static_cast<int>(year))) {
				damaged();
			}
			return Date{ static_cast<int>(year), static_cast<int>(month), static_cast<int>(day) };
		}
		case 'E': {
			std::int64_t ordinal = number(';');
			if (!type || type->kind != TypeKind::Enum || ordinal < 0
				|| ordinal >= static_cast<std::int64_t>(type->enumValues.size())) {
				damaged();
			}
			return EnumValue{ type, static_cast<int>(ordinal) };
		}
		default:
			damaged();
		}
	}

	std::string_view data_;
	size_t pos_ = 0;
	std::string damaged_;
};

using Slots = std::vector<std::optional<std::string>>;

std::optional<Slots> parseRandomFile(const std::string& content) {
	if (!content.starts_with(RandomHeader)) {
		return std::nullopt;
	}
	size_t pos = RandomHeader.size();
	auto line = [&]() -> std::optional<std::string_view> {
		size_t end = content.find('\n', pos);
		if (end == std::string::npos) return std::nullopt;
		std::string_view text(content.data() + pos, end - pos);
		pos = end + 1;
		return text;
	};
	auto count = [](std::string_view text) -> std::optional<size_t> {
		size_t value = 0;
		auto result = std::from_chars(text.data(), text.data() + text.size(), value);
		if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return std::nullopt;
		return value;
	};

	auto slotCount = line();
	auto slots = slotCount ? count(*slotCount) : std::nullopt;
	if (!slots || *slots > static_cast<size_t>(MaxRecordAddress)) {
		return std::nullopt;
	}
	Slots result;
	result.reserve(*slots);
	for (size_t i = 0; i < *slots; ++i) {
		auto header = line();
		if (!header) return std::nullopt;
		if (*header == "-") {
			result.emplace_back();
			continue;
		}
		auto length = count(*header);
		if (!length || *length > content.size() - pos || pos + *length >= content.size() || content[pos + *length] != '\n') {
			return std::nullopt;
		}
		result.emplace_back(content.substr(pos, *length));
		pos += *length + 1;
	}
	return result;
}

std::string serializeRandomFile(const Slots& slots) {
	std::string out(RandomHeader);
	out += std::to_string(slots.size()) + '\n';
	for (const std::optional<std::string>& slot : slots) {
		if (!slot) {
			out += "-\n";
			continue;
		}
		out += std::to_string(slot->size()) + '\n';
		out += *slot;
		out += '\n';
	}
	return out;
}

bool writeWholeFile(const std::filesystem::path& path, const std::string& content) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << content;
	out.close();
	return static_cast<bool>(out);
}

}

// ---------- the open-file table ----------

Files::Files(std::filesystem::path baseDirectory) : baseDirectory_(std::move(baseDirectory)) {}

Files::~Files() {
	try {
		closeAll();
	}
	catch (...) {
		// Nothing more can be done for a file that cannot be written.
	}
}

std::filesystem::path Files::resolve(const std::string& name) const {
	// From UTF-8, so names that are not plain ASCII also work on Windows.
	std::filesystem::path path(std::u8string(name.begin(), name.end()));
	return path.is_absolute() ? path : baseDirectory_ / path;
}

// Spec 10.1, 10.2.
void Files::open(const std::string& name, FileMode mode, SourceLocation openedAt) {
	if (name.empty()) {
		fileError("A file name cannot be empty");
	}
	std::string key = toUpper(name);
	if (auto existing = files_.find(key); existing != files_.end()) {
		fileError("The file " + inQuotes(name) + " is already open for " + modeName(existing->second->mode)
			+ ". Close it with CLOSEFILE before opening it again");
	}

	auto file = std::make_unique<OpenFile>();
	file->name = name;
	file->path = resolve(name);
	file->mode = mode;
	file->openedAt = openedAt;
	std::error_code error;
	bool exists = std::filesystem::exists(file->path, error);
	if (exists && std::filesystem::is_directory(file->path, error)) {
		fileError(inQuotes(name) + " is a folder, not a file");
	}

	switch (mode) {
	case FileMode::Read: {
		if (!exists) {
			fileError("The file " + inQuotes(name) + " does not exist. Files are looked for in the same folder as the program");
		}
		std::optional<std::string> content = readWholeFile(file->path);
		if (!content) {
			fileError("The file " + inQuotes(name) + " exists but cannot be read");
		}
		file->lines = splitLines(*content);
		break;
	}
	case FileMode::Write:
	case FileMode::Append:
		// Binary, so lines end with \n on every platform.
		file->stream.open(file->path, std::ios::binary | (mode == FileMode::Write ? std::ios::trunc : std::ios::app));
		if (!file->stream) {
			fileError("Cannot " + std::string(mode == FileMode::Write ? "create" : "open") + " the file " + inQuotes(name)
				+ " for " + modeName(mode));
		}
		break;
	case FileMode::Random:
		if (exists) {
			std::optional<std::string> content = readWholeFile(file->path);
			if (!content) {
				fileError("The file " + inQuotes(name) + " exists but cannot be read");
			}
			if (!content->empty()) {
				std::optional<Slots> slots = parseRandomFile(*content);
				if (!slots) {
					fileError(inQuotes(name) + " is not a random file written by this interpreter, so it cannot be opened FOR RANDOM");
				}
				file->slots = std::move(*slots);
			}
		}
		else if (!writeWholeFile(file->path, serializeRandomFile({}))) {
			fileError("Cannot create the file " + inQuotes(name));
		}
		break;
	}

	files_[key] = std::move(file);
	openOrder_.push_back(std::move(key));
}

Files::OpenFile& Files::find(const std::string& name, std::initializer_list<FileMode> modes, std::string_view statement) {
	auto found = files_.find(toUpper(name));
	if (found == files_.end()) {
		fileError("The file " + inQuotes(name) + " is not open, so " + std::string(statement)
			+ " cannot use it. Open it first with OPENFILE " + inQuotes(name) + " FOR " + modeName(*modes.begin()));
	}
	OpenFile& file = *found->second;
	if (std::find(modes.begin(), modes.end(), file.mode) == modes.end()) {
		std::string needed;
		for (FileMode mode : modes) {
			needed += (needed.empty() ? "" : " or ") + modeName(mode);
		}
		fileError("The file " + inQuotes(name) + " is open for " + modeName(file.mode) + ", but "
			+ std::string(statement) + " needs it open for " + needed);
	}
	return file;
}

void Files::finish(OpenFile& file) {
	switch (file.mode) {
	case FileMode::Read:
		break;
	case FileMode::Write:
	case FileMode::Append:
		file.stream.close();
		if (!file.stream) {
			fileError("Could not finish writing the file " + inQuotes(file.name));
		}
		break;
	case FileMode::Random:
		if (!writeWholeFile(file.path, serializeRandomFile(file.slots))) {
			fileError("Could not write the file " + inQuotes(file.name));
		}
		break;
	}
}

void Files::close(const std::string& name) {
	std::string key = toUpper(name);
	auto found = files_.find(key);
	if (found == files_.end()) {
		fileError("The file " + inQuotes(name) + " is not open, so CLOSEFILE cannot close it");
	}
	std::unique_ptr<OpenFile> file = std::move(found->second);
	files_.erase(found);
	openOrder_.erase(std::find(openOrder_.begin(), openOrder_.end(), key));
	finish(*file);
}

std::vector<Files::LeftOpen> Files::closeAll() {
	std::vector<LeftOpen> closed;
	for (const std::string& key : openOrder_) {
		OpenFile& file = *files_.at(key);
		LeftOpen entry{ file.name, file.openedAt, {} };
		try {
			finish(file);
		}
		catch (const RuntimeError& error) {
			entry.error = error.what();
		}
		closed.push_back(std::move(entry));
	}
	files_.clear();
	openOrder_.clear();
	return closed;
}

// ---------- text files ----------

std::string Files::readLine(const std::string& name) {
	OpenFile& file = find(name, { FileMode::Read }, "READFILE");
	if (file.nextLine >= file.lines.size()) {
		fileError("There are no more lines to read in " + inQuotes(name) + ". Check EOF(" + inQuotes(name) + ") before reading");
	}
	return file.lines[file.nextLine++];
}

bool Files::atEnd(const std::string& name) {
	OpenFile& file = find(name, { FileMode::Read }, "EOF");
	return file.nextLine >= file.lines.size();
}

void Files::writeLine(const std::string& name, const std::string& line) {
	OpenFile& file = find(name, { FileMode::Write, FileMode::Append }, "WRITEFILE");
	file.stream << line << '\n';
	if (!file.stream) {
		fileError("Could not write to the file " + inQuotes(name));
	}
}

// ---------- random files ----------

void Files::seek(const std::string& name, std::int64_t address) {
	OpenFile& file = find(name, { FileMode::Random }, "SEEK");
	if (address < 1) {
		fileError("A record address must be 1 or more, but it is " + std::to_string(address)
			+ ". Record 1 is the first record");
	}
	if (address > MaxRecordAddress) {
		fileError("A record address can be at most " + std::to_string(MaxRecordAddress)
			+ ", but it is " + std::to_string(address));
	}
	file.pointer = address;
}

void Files::getRecord(const std::string& name, Cell& target, const std::string& targetName) {
	OpenFile& file = find(name, { FileMode::Random }, "GETRECORD");
	std::string where = "Record " + std::to_string(file.pointer) + " of " + inQuotes(name);
	size_t index = static_cast<size_t>(file.pointer - 1);
	if (index >= file.slots.size() || !file.slots[index]) {
		fileError(where + " is empty. Write it with PUTRECORD first, or SEEK to another record");
	}

	const std::string& payload = *file.slots[index];
	size_t firstBreak = payload.find('\n');
	size_t secondBreak = firstBreak == std::string::npos ? std::string::npos : payload.find('\n', firstBreak + 1);
	if (secondBreak == std::string::npos) {
		fileError(where + " is damaged");
	}
	std::string storedName = payload.substr(0, firstBreak);
	std::string storedSignature = payload.substr(firstBreak + 1, secondBreak - firstBreak - 1);

	if (target.type) {
		if (signature(target) != storedSignature) {
			throw RuntimeError("R002", where + " holds a value of type " + storedName + ", but " + targetName
				+ " is " + describeType(target.type) + ". GETRECORD needs a variable of exactly the stored type");
		}
	}
	else if (!isAtomicSignature(storedSignature)) {
		throw RuntimeError("R002", "Declare " + targetName + " as " + storedName + " before GETRECORD reads "
			+ where);
	}

	Decoder decoder(std::string_view(payload).substr(secondBreak + 1), where + " is damaged");
	decoder.into(target);
	if (!decoder.done()) {
		fileError(where + " is damaged");
	}
}

void Files::putRecord(const std::string& name, const Cell& source, const std::string& sourceName) {
	OpenFile& file = find(name, { FileMode::Random }, "PUTRECORD");
	if (isUnassigned(source.value)) {
		throw RuntimeError("R001", sourceName + " used before it was given a value");
	}
	const Type* type = source.type ? source.type : typeOf(source.value);
	std::string payload = typeName(type) + '\n' + signature(source) + '\n';
	encode(source.value, payload);

	size_t index = static_cast<size_t>(file.pointer - 1);
	if (index >= file.slots.size()) {
		file.slots.resize(index + 1); // empty slots in between (spec 10.2)
	}
	file.slots[index] = std::move(payload);
}
