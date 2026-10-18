#include "Builtins.h"

#include <array>
#include <charconv>
#include <cmath>
#include <ctime>

#include "Files.h"
#include "Operations.h"

namespace {

enum class Id {
	Length, Left, Right, Mid, Lcase, Ucase, ToLower, ToUpper,
	NumToStr, StrToNum, IsNum, Asc, Chr,
	Int, Rand, Round, Mod, Div,
	Day, Month, Year, DayIndex, SetDate, Now,
	Eof,
};

// Parameter kinds: S STRING (a CHAR is accepted, spec 4.3), C CHAR,
// I INTEGER, R REAL (an INTEGER is accepted), N INTEGER or REAL,
// T CHAR or STRING, kept as it is, D DATE.
// Result: I INTEGER, R REAL, C CHAR, S STRING, B BOOLEAN, D DATE,
// = the same type as the first argument, ? known only when it runs.
struct Function {
	std::string_view name;
	Id id;
	std::array<std::string_view, 3> parameters;
	size_t count;
	std::string_view kinds;
	char result;
};

constexpr std::array<Function, 25> functions = { {
	// Spec 9.1: string and character functions
	{ "LENGTH", Id::Length, { "S" }, 1, "S", 'I' },
	{ "LEFT", Id::Left, { "S", "X" }, 2, "SI", 'S' },
	{ "RIGHT", Id::Right, { "S", "X" }, 2, "SI", 'S' },
	{ "MID", Id::Mid, { "S", "X", "Y" }, 3, "SII", 'S' },
	{ "LCASE", Id::Lcase, { "C" }, 1, "C", 'C' },
	{ "UCASE", Id::Ucase, { "C" }, 1, "C", 'C' },
	{ "TO_LOWER", Id::ToLower, { "S" }, 1, "T", '=' },
	{ "TO_UPPER", Id::ToUpper, { "S" }, 1, "T", '=' },
	{ "NUM_TO_STR", Id::NumToStr, { "N" }, 1, "N", 'S' },
	{ "STR_TO_NUM", Id::StrToNum, { "S" }, 1, "S", '?' },
	{ "IS_NUM", Id::IsNum, { "S" }, 1, "S", 'B' },
	{ "ASC", Id::Asc, { "C" }, 1, "C", 'I' },
	{ "CHR", Id::Chr, { "X" }, 1, "I", 'C' },
	// Spec 9.2: numeric functions
	{ "INT", Id::Int, { "X" }, 1, "R", 'I' },
	{ "RAND", Id::Rand, { "X" }, 1, "I", 'R' },
	{ "ROUND", Id::Round, { "X", "Places" }, 2, "RI", 'R' },
	{ "MOD", Id::Mod, { "A", "B" }, 2, "II", 'I' },
	{ "DIV", Id::Div, { "A", "B" }, 2, "II", 'I' },
	// Spec 9.3: date functions
	{ "DAY", Id::Day, { "D" }, 1, "D", 'I' },
	{ "MONTH", Id::Month, { "D" }, 1, "D", 'I' },
	{ "YEAR", Id::Year, { "D" }, 1, "D", 'I' },
	{ "DAYINDEX", Id::DayIndex, { "D" }, 1, "D", 'I' },
	{ "SETDATE", Id::SetDate, { "Day", "Month", "Year" }, 3, "III", 'D' },
	{ "NOW", Id::Now, {}, 0, "", 'D' },
	// Spec 9.4: file function
	{ "EOF", Id::Eof, { "F" }, 1, "S", 'B' },
} };

std::string toUpper(std::string_view text) {
	std::string result(text);
	for (char& c : result) {
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}
	return result;
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

const Function* findFunction(std::string_view name) {
	std::string key = toUpper(name);
	for (const Function& function : functions) {
		if (function.name == key) {
			return &function;
		}
	}
	return nullptr;
}

// "MID(S, X, Y)"
std::string signatureOf(const Function& function) {
	std::string text(function.name);
	text += '(';
	for (size_t i = 0; i < function.count; ++i) {
		text += (i > 0 ? ", " : "") + std::string(function.parameters[i]);
	}
	return text + ')';
}

// Why an argument of type `actual` does not fit parameter `i`, or nothing if
// it does. A null `actual` is unknown before running, which passes, but at
// runtime is a null object, which does not.
std::optional<std::string> argumentProblem(const Function& function, size_t i, const Type* actual, bool atRuntime) {
	if (!actual && !atRuntime) {
		return std::nullopt;
	}
	auto is = [&](TypeKind kind) { return actual && actual->kind == kind; };
	bool text = actual && actual->isText();
	bool number = actual && actual->isNumeric();

	std::string_view expected;
	std::string hint;
	switch (function.kinds[i]) {
	case 'S':
		if (text) return std::nullopt;
		expected = "a STRING";
		if (number) hint = "Use NUM_TO_STR to turn a number into text";
		break;
	case 'C':
		if (is(TypeKind::Char)) return std::nullopt;
		expected = "a CHAR";
		if (is(TypeKind::String)) {
			hint = function.id == Id::Lcase ? "Use TO_LOWER for a STRING"
				: function.id == Id::Ucase ? "Use TO_UPPER for a STRING"
				: "A STRING cannot become a CHAR, even if it is one character long";
		}
		break;
	case 'I':
		if (is(TypeKind::Integer)) return std::nullopt;
		expected = "an INTEGER";
		if (is(TypeKind::Real)) hint = "Use INT() to convert a REAL";
		if (text) hint = "Use STR_TO_NUM to turn text into a number";
		break;
	case 'R':
		if (number) return std::nullopt;
		expected = "a REAL";
		if (text) hint = "Use STR_TO_NUM to turn text into a number";
		break;
	case 'N':
		if (number) return std::nullopt;
		expected = "an INTEGER or REAL";
		break;
	case 'T':
		if (text) return std::nullopt;
		expected = "a CHAR or STRING";
		break;
	case 'D':
		if (is(TypeKind::Date)) return std::nullopt;
		expected = "a DATE";
		if (text) hint = "Write a date as dd/mm/yyyy, or build one with SETDATE";
		break;
	}
	std::string message = std::string(function.name) + " needs " + std::string(expected) + " for "
		+ std::string(function.parameters[i]) + ", but this is " + (actual ? describeType(actual) : "a null object");
	if (!hint.empty()) {
		message += ". " + hint;
	}
	return message;
}

BuiltinCheck check(const Function& function, const std::vector<const Type*>& argumentTypes, bool atRuntime) {
	if (argumentTypes.size() != function.count) {
		return { nullptr, signatureOf(function) + " takes " + std::to_string(function.count)
			+ (function.count == 1 ? " argument" : " arguments") + ", but " + std::to_string(argumentTypes.size())
			+ (argumentTypes.size() == 1 ? " was" : " were") + " given", "M003", 0 };
	}
	for (size_t i = 0; i < argumentTypes.size(); ++i) {
		if (std::optional<std::string> problem = argumentProblem(function, i, argumentTypes[i], atRuntime)) {
			return { nullptr, std::move(*problem), atRuntime ? "R002" : "M004", i };
		}
	}
	BuiltinCheck allowed;
	switch (function.result) {
	case 'I': allowed.result = integerType(); break;
	case 'R': allowed.result = realType(); break;
	case 'C': allowed.result = charType(); break;
	case 'S': allowed.result = stringType(); break;
	case 'B': allowed.result = booleanType(); break;
	case 'D': allowed.result = dateType(); break;
	case '=': allowed.result = argumentTypes[0]; break;
	default:  break;
	}
	return allowed;
}

// One call's arguments, already checked against the function's kinds.
class Arguments {
public:
	explicit Arguments(const std::vector<Value>& values) : values_(values) {}

	const Value& value(size_t i) const { return values_[i]; }

	std::string text(size_t i) const {
		if (const auto* c = std::get_if<char>(&values_[i])) return std::string(1, *c);
		return std::get<std::string>(values_[i]);
	}

	char character(size_t i) const { return std::get<char>(values_[i]); }
	std::int64_t integer(size_t i) const { return std::get<std::int64_t>(values_[i]); }
	Date date(size_t i) const { return std::get<Date>(values_[i]); }

	double real(size_t i) const {
		if (const auto* n = std::get_if<std::int64_t>(&values_[i])) return static_cast<double>(*n);
		return std::get<double>(values_[i]);
	}

private:
	const std::vector<Value>& values_;
};

[[noreturn]] void invalid(const std::string& message) {
	throw RuntimeError("R006", message);
}

// Spec 9.2: X rounded to `places` decimal places, halves away from zero.
// Rounds the shortest decimal form of X, the digits OUTPUT shows, so that
// ROUND(2.675, 2) is 2.68 even though the nearest double is 2.67499...
double roundDecimal(double value, std::int64_t places) {
	char buffer[512];
	auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed);
	std::string text(buffer, result.ptr);
	size_t point = text.find('.');
	if (point == std::string::npos || static_cast<std::int64_t>(text.size() - point - 1) <= places) {
		return value;
	}
	size_t firstDropped = point + 1 + static_cast<size_t>(places);
	bool roundUp = text[firstDropped] >= '5';
	text.resize(places == 0 ? point : firstDropped);

	if (roundUp) {
		// Add one to the last kept digit, carrying left.
		size_t i = text.size();
		while (i > 0) {
			char& digit = text[i - 1];
			if (digit == '.') {
				--i;
				continue;
			}
			if (digit == '-') {
				break;
			}
			if (digit != '9') {
				++digit;
				break;
			}
			digit = '0';
			--i;
		}
		if (i == 0 || text[i - 1] == '-') {
			text.insert(i, "1"); // 9.96 to one place becomes 10.0
		}
	}

	double rounded = 0.0;
	std::from_chars(text.data(), text.data() + text.size(), rounded);
	return rounded == 0.0 ? 0.0 : rounded; // no -0.0
}

// Days since 1 January 1970 in the proleptic Gregorian calendar
// (Howard Hinnant's days_from_civil).
std::int64_t daysFromCivil(int year, int month, int day) {
	year -= month <= 2;
	const int era = (year >= 0 ? year : year - 399) / 400;
	const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
	const unsigned dayOfYear = static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
	const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
	return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(dayOfEra) - 719468;
}

// Spec 9.3: Sunday = 1 ... Saturday = 7. 1 January 1970 was a Thursday.
std::int64_t dayIndex(const Date& date) {
	std::int64_t days = daysFromCivil(date.year, date.month, date.day);
	std::int64_t weekday = ((days + 4) % 7 + 7) % 7; // 0 = Sunday
	return weekday + 1;
}

Date today() {
	std::time_t now = std::time(nullptr);
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	return Date{ local.tm_year + 1900, local.tm_mon + 1, local.tm_mday };
}

}

bool isBuiltinFunction(std::string_view name) {
	return findFunction(name) != nullptr;
}

BuiltinCheck checkBuiltinCall(std::string_view name, const std::vector<const Type*>& argumentTypes) {
	const Function* function = findFunction(name);
	if (!function) {
		return { nullptr, "There is no built-in function called " + std::string(name), "M001", 0 };
	}
	return check(*function, argumentTypes, false);
}

Builtins::Builtins(std::optional<std::uint64_t> seed, Files& files) : files_(files) {
	if (seed) {
		generator_.seed(*seed);
	}
	else {
		std::random_device device;
		generator_.seed((static_cast<std::uint64_t>(device()) << 32) ^ device());
	}
}

double Builtins::random(std::int64_t limit) {
	// The top 53 bits make a double in [0, 1) with every value equally likely.
	double unit = static_cast<double>(generator_() >> 11) * 0x1.0p-53;
	double bound = static_cast<double>(limit);
	double result = unit * bound;
	return result < bound ? result : std::nextafter(bound, 0.0);
}

Value Builtins::call(std::string_view name, std::vector<Value> values) {
	const Function* function = findFunction(name);
	if (!function) {
		throw RuntimeError("M001", "There is no built-in function called " + std::string(name));
	}
	std::vector<const Type*> types;
	for (const Value& value : values) {
		types.push_back(typeOf(value));
	}
	BuiltinCheck problem = check(*function, types, true);
	if (!problem.ok()) {
		if (problem.code == "M003") {
			throw RuntimeError("M003", problem.error);
		}
		throw ArgumentError(problem.argument, problem.error);
	}
	Arguments arguments(values);

	switch (function->id) {
	case Id::Length:
		return static_cast<std::int64_t>(arguments.text(0).size());

	case Id::Left:
	case Id::Right: {
		std::string s = arguments.text(0);
		std::int64_t x = arguments.integer(1);
		std::int64_t length = static_cast<std::int64_t>(s.size());
		if (x < 0 || x > length) {
			invalid(std::string(function->name) + " can take from 0 to " + std::to_string(length)
				+ " characters of this string, but X is " + std::to_string(x));
		}
		return function->id == Id::Left ? s.substr(0, static_cast<size_t>(x)) : s.substr(static_cast<size_t>(length - x));
	}

	case Id::Mid: {
		std::string s = arguments.text(0);
		std::int64_t x = arguments.integer(1);
		std::int64_t y = arguments.integer(2);
		std::int64_t length = static_cast<std::int64_t>(s.size());
		if (x < 1) {
			invalid("MID's start position X must be at least 1, but it is " + std::to_string(x));
		}
		if (y < 0) {
			invalid("MID's length Y cannot be negative, but it is " + std::to_string(y));
		}
		if (x - 1 > length || y > length - (x - 1)) {
			invalid("MID would need characters " + std::to_string(x) + " to " + std::to_string(x + y - 1)
				+ ", but the string has only " + std::to_string(length));
		}
		return s.substr(static_cast<size_t>(x - 1), static_cast<size_t>(y));
	}

	case Id::Lcase:
	case Id::Ucase: {
		char c = arguments.character(0);
		return function->id == Id::Lcase ? lower(c) : upper(c);
	}

	case Id::ToLower:
	case Id::ToUpper: {
		auto convert = function->id == Id::ToLower ? lower : upper;
		if (const auto* c = std::get_if<char>(&arguments.value(0))) {
			return convert(*c);
		}
		std::string result = std::get<std::string>(arguments.value(0));
		for (char& c : result) {
			c = convert(c);
		}
		return result;
	}

	case Id::NumToStr:
		return formatValue(arguments.value(0));

	case Id::StrToNum: {
		std::string s = arguments.text(0);
		if (std::optional<Value> number = parseNumber(s)) {
			return *number;
		}
		invalid("STR_TO_NUM needs text that is a number, such as \"42\" or \"-3.5\", but it was given \"" + s + "\"");
	}

	case Id::IsNum:
		return isNumberText(arguments.text(0));

	case Id::Asc:
		return static_cast<std::int64_t>(static_cast<unsigned char>(arguments.character(0)));

	case Id::Chr: {
		std::int64_t code = arguments.integer(0);
		if (code < 0 || code > 127) {
			invalid("CHR needs a character code from 0 to 127, but it was given " + std::to_string(code));
		}
		return static_cast<char>(code);
	}

	case Id::Int: {
		if (std::holds_alternative<std::int64_t>(arguments.value(0))) {
			return arguments.value(0); // already whole; avoids losing digits through REAL
		}
		double truncated = std::trunc(arguments.real(0));
		// The INTEGER range is [-2^63, 2^63).
		if (!(truncated >= -0x1.0p63 && truncated < 0x1.0p63)) {
			throw RuntimeError("R005", "INT(" + formatReal(arguments.real(0)) + ") is too large for an INTEGER");
		}
		return static_cast<std::int64_t>(truncated);
	}

	case Id::Rand: {
		std::int64_t limit = arguments.integer(0);
		if (limit <= 0) {
			invalid("RAND needs a limit above 0, but it was given " + std::to_string(limit));
		}
		return random(limit);
	}

	case Id::Round: {
		double x = arguments.real(0);
		std::int64_t places = arguments.integer(1);
		if (places < 0) {
			invalid("ROUND needs 0 or more decimal places, but it was given " + std::to_string(places));
		}
		return roundDecimal(x, places);
	}

	case Id::Mod:
	case Id::Div: {
		std::int64_t a = arguments.integer(0);
		std::int64_t b = arguments.integer(1);
		return applyBinary(function->id == Id::Mod ? BinaryOp::Mod : BinaryOp::IntDiv, a, b);
	}

	case Id::Day:      return static_cast<std::int64_t>(arguments.date(0).day);
	case Id::Month:    return static_cast<std::int64_t>(arguments.date(0).month);
	case Id::Year:     return static_cast<std::int64_t>(arguments.date(0).year);
	case Id::DayIndex: return dayIndex(arguments.date(0));

	case Id::SetDate: {
		std::int64_t day = arguments.integer(0);
		std::int64_t month = arguments.integer(1);
		std::int64_t year = arguments.integer(2);
		bool inRange = day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 1 && year <= 9999;
		if (!inRange || !isValidDate(static_cast<int>(day), static_cast<int>(month), static_cast<int>(year))) {
			invalid("SETDATE(" + std::to_string(day) + ", " + std::to_string(month) + ", " + std::to_string(year)
				+ ") is not a valid date. The order is day, month, year");
		}
		return Date{ static_cast<int>(year), static_cast<int>(month), static_cast<int>(day) };
	}

	case Id::Now:
		return today();

	case Id::Eof:
		return files_.atEnd(arguments.text(0));
	}
	return {};
}
