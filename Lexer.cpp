#include "Lexer.h"

#include <charconv>
#include <cstdio>

namespace {

// Unicode aliases accepted so code pasted from exam papers lexes cleanly (spec 2.4).
constexpr char32_t LeftwardsArrow = 0x2190;     // assignment
constexpr char32_t EnDash = 0x2013;             // minus
constexpr char32_t MinusSign = 0x2212;          // minus
constexpr char32_t NotEqualTo = 0x2260;
constexpr char32_t LessThanOrEqualTo = 0x2264;
constexpr char32_t GreaterThanOrEqualTo = 0x2265;
constexpr char32_t LeftDoubleQuote = 0x201C;
constexpr char32_t RightDoubleQuote = 0x201D;
constexpr char32_t LeftSingleQuote = 0x2018;
constexpr char32_t RightSingleQuote = 0x2019;
constexpr char32_t LatinSaltillo = 0xA78C;      // looks like an apostrophe
constexpr char32_t NoBreakSpace = 0x00A0;       // common in text copied from PDFs
constexpr char32_t ByteOrderMark = 0xFEFF;
constexpr char32_t MultiplicationSign = 0x00D7;
constexpr char32_t DivisionSign = 0x00F7;

bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isLetter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isWordChar(unsigned char c) { return isLetter(c) || isDigit(c) || c == '_'; }

bool isCharQuote(char32_t c) {
	return c == '\'' || c == LeftSingleQuote || c == RightSingleQuote || c == LatinSaltillo;
}

bool isStringQuote(char32_t c) {
	return c == '"' || c == LeftDoubleQuote || c == RightDoubleQuote;
}

std::string toUpper(std::string_view text) {
	std::string result(text);
	for (char& c : result) {
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}
	return result;
}

std::string codePointName(char32_t c) {
	char buffer[16];
	std::snprintf(buffer, sizeof buffer, "U+%04X", static_cast<unsigned>(c));
	return buffer;
}

// Spec 2.6 rule 2: a NEWLINE after one of these is a line continuation.
bool cannotEndStatement(TokenType type) {
	switch (type) {
	case TokenType::Comma: case TokenType::Assign: case TokenType::Colon:
	case TokenType::Plus: case TokenType::Minus: case TokenType::Star: case TokenType::Slash:
	case TokenType::Ampersand: case TokenType::Equal: case TokenType::NotEqual:
	case TokenType::Less: case TokenType::Greater: case TokenType::LessEqual: case TokenType::GreaterEqual:
	case TokenType::Div: case TokenType::Mod: case TokenType::In:
	case TokenType::And: case TokenType::Or: case TokenType::Not:
	case TokenType::Of: case TokenType::To: case TokenType::Step:
		return true;
	default:
		return false;
	}
}

// Spec 2.6 rule 3: a NEWLINE before one of these is a line continuation.
bool cannotStartStatement(TokenType type) {
	switch (type) {
	case TokenType::Then: case TokenType::Of: case TokenType::Returns: case TokenType::To:
	case TokenType::Plus: case TokenType::Star: case TokenType::Slash: case TokenType::Ampersand:
	case TokenType::Equal: case TokenType::NotEqual:
	case TokenType::Less: case TokenType::Greater: case TokenType::LessEqual: case TokenType::GreaterEqual:
	case TokenType::Div: case TokenType::Mod: case TokenType::In:
	case TokenType::And: case TokenType::Or:
	case TokenType::RightParen: case TokenType::RightBracket:
		return true;
	default:
		return false;
	}
}

}

Lexer::Lexer(std::string_view source) : source_(source) {}

bool Lexer::hasErrors() const {
	for (const Diagnostic& diagnostic : diagnostics_) {
		if (diagnostic.severity == Severity::Error) {
			return true;
		}
	}
	return false;
}

std::vector<Token> Lexer::tokenize() {
	if (source_.starts_with("\xEF\xBB\xBF")) {
		pos_ = 3;
	}

	while (!atEnd()) {
		unsigned char c = peek();

		if (c == ' ' || c == '\t' || c == '\f' || c == '\v' || (c == '\r' && peek(1) == '\n')) {
			advance();
		}
		else if (c == '\n' || c == '\r') {
			Token token;
			token.type = TokenType::Newline;
			token.line = line_;
			token.column = column_;
			advance();
			emit(std::move(token));
		}
		else if (c == '/' && peek(1) == '/') {
			skipComment();
		}
		else if (isDigit(c)) {
			lexNumber();
		}
		else if (c == '.' && isDigit(peek(1))) {
			lexLeadingDotNumber();
		}
		else if (isLetter(c) || c == '_') {
			lexWord();
		}
		else if (c < 0x80) {
			lexAsciiSymbol();
		}
		else {
			lexNonAscii();
		}
	}

	if (!tokens_.empty() && tokens_.back().type != TokenType::Newline) {
		Token newline;
		newline.type = TokenType::Newline;
		newline.line = line_;
		newline.column = column_;
		tokens_.push_back(std::move(newline));
	}
	Token end;
	end.type = TokenType::EndOfFile;
	end.line = line_;
	end.column = column_;
	tokens_.push_back(std::move(end));

	return std::move(tokens_);
}

unsigned char Lexer::peek(size_t ahead) const {
	size_t index = pos_ + ahead;
	return index < source_.size() ? static_cast<unsigned char>(source_[index]) : '\0';
}

Lexer::CodePoint Lexer::decodeAt(size_t pos) const {
	auto byteAt = [&](size_t i) -> unsigned char {
		return i < source_.size() ? static_cast<unsigned char>(source_[i]) : 0;
	};
	unsigned char lead = byteAt(pos);
	int length;
	char32_t value;
	if (lead < 0x80) {
		return { lead, 1, true };
	}
	else if ((lead & 0xE0) == 0xC0) {
		length = 2;
		value = lead & 0x1F;
	}
	else if ((lead & 0xF0) == 0xE0) {
		length = 3;
		value = lead & 0x0F;
	}
	else if ((lead & 0xF8) == 0xF0) {
		length = 4;
		value = lead & 0x07;
	}
	else {
		return { lead, 1, false };
	}
	for (int i = 1; i < length; ++i) {
		unsigned char next = byteAt(pos + i);
		if ((next & 0xC0) != 0x80) {
			return { lead, 1, false };
		}
		value = (value << 6) | (next & 0x3F);
	}
	return { value, length, true };
}

void Lexer::advance() {
	unsigned char c = peek();
	++pos_;
	if (c == '\n' || (c == '\r' && peek() != '\n')) {
		++line_;
		column_ = 1;
	}
	else if ((c & 0xC0) != 0x80) {
		// Columns count code points, so continuation bytes do not move the column.
		++column_;
	}
}

void Lexer::advanceBytes(size_t count) {
	for (size_t i = 0; i < count && !atEnd(); ++i) {
		advance();
	}
}

void Lexer::skipComment() {
	while (!atEnd() && peek() != '\n' && peek() != '\r') {
		advance();
	}
}

void Lexer::lexNumber() {
	size_t start = pos_;
	int line = line_;
	int column = column_;

	if (tryLexDate()) {
		return;
	}

	while (isDigit(peek())) {
		advance();
	}
	size_t numberEnd = pos_;
	bool isReal = false;
	bool malformed = false;

	if (peek() == '.') {
		if (isDigit(peek(1))) {
			isReal = true;
			advance();
			while (isDigit(peek())) {
				advance();
			}
			numberEnd = pos_;
		}
		else {
			// "5." - recover by reading it as 5.0.
			isReal = true;
			malformed = true;
			advance();
			std::string digits(source_.substr(start, numberEnd - start));
			error("L004", line, column,
				"'" + digits + ".' is not a valid number: a REAL needs a digit after the decimal point. Write " + digits + ".0");
		}
	}

	if (isLetter(peek()) || peek() == '_') {
		while (isWordChar(peek())) {
			advance();
		}
		if (!malformed) {
			error("L004", line, column,
				"'" + std::string(source_.substr(start, pos_ - start)) + "' is not a valid number. Names must start with a letter");
		}
	}

	std::string_view digits = source_.substr(start, numberEnd - start);
	if (isReal) {
		double value = 0.0;
		std::from_chars(digits.data(), digits.data() + digits.size(), value);
		addToken(TokenType::RealLiteral, start, line, column, value);
	}
	else {
		std::int64_t value = 0;
		auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
		if (result.ec == std::errc::result_out_of_range) {
			error("L004", line, column, "The number " + std::string(digits) + " is too large for an INTEGER");
			value = 0;
		}
		addToken(TokenType::IntegerLiteral, start, line, column, value);
	}
}

// Recognises d{1,2}/d{1,2}/d{4} with no whitespace (spec 2.3). Returns false,
// consuming nothing, if the text does not have that shape.
bool Lexer::tryLexDate() {
	size_t p = pos_;
	auto digitRun = [&]() {
		size_t runStart = p;
		while (p < source_.size() && isDigit(static_cast<unsigned char>(source_[p]))) {
			++p;
		}
		return p - runStart;
	};
	auto slash = [&]() {
		if (p < source_.size() && source_[p] == '/') {
			++p;
			return true;
		}
		return false;
	};

	size_t dayLength = digitRun();
	if (dayLength < 1 || dayLength > 2 || !slash()) return false;
	size_t monthLength = digitRun();
	if (monthLength < 1 || monthLength > 2 || !slash()) return false;
	if (digitRun() != 4) return false;

	size_t start = pos_;
	int line = line_;
	int column = column_;
	std::string_view text = source_.substr(start, p - start);
	advanceBytes(p - start);

	auto parse = [](std::string_view part) {
		int value = 0;
		std::from_chars(part.data(), part.data() + part.size(), value);
		return value;
	};
	size_t firstSlash = text.find('/');
	size_t secondSlash = text.find('/', firstSlash + 1);
	Date date;
	date.day = parse(text.substr(0, firstSlash));
	date.month = parse(text.substr(firstSlash + 1, secondSlash - firstSlash - 1));
	date.year = parse(text.substr(secondSlash + 1));

	if (!isValidDate(date.day, date.month, date.year)) {
		std::string message = "'" + std::string(text) + "' is not a valid date";
		if (date.month > 12 && date.day <= 12) {
			message += ". Dates are written day/month/year (dd/mm/yyyy)";
		}
		message += ". To divide, put spaces around '/'";
		error("L004", line, column, std::move(message));
	}
	addToken(TokenType::DateLiteral, start, line, column, date);
	return true;
}

void Lexer::lexLeadingDotNumber() {
	size_t start = pos_;
	int line = line_;
	int column = column_;
	advance(); // '.'
	while (isDigit(peek())) {
		advance();
	}
	std::string_view digits = source_.substr(start + 1, pos_ - start - 1);
	error("L004", line, column,
		"'." + std::string(digits) + "' is not a valid number: a REAL needs a digit before the decimal point. Write 0." + std::string(digits));
	double value = 0.0;
	std::string text = "0." + std::string(digits);
	std::from_chars(text.data(), text.data() + text.size(), value);
	addToken(TokenType::RealLiteral, start, line, column, value);
}

void Lexer::lexWord() {
	size_t start = pos_;
	int line = line_;
	int column = column_;
	while (isWordChar(peek())) {
		advance();
	}
	std::string_view word = source_.substr(start, pos_ - start);

	if (word.front() == '_') {
		error("L002", line, column, "'" + std::string(word) + "' is not a valid name: names must start with a letter");
		addToken(TokenType::Identifier, start, line, column);
		return;
	}

	std::string upper = toUpper(word);
	if (auto keyword = lookupKeyword(upper)) {
		if (word != upper) {
			error("L003", line, column, "Keywords must be upper case — did you mean " + upper + "?");
		}
		addToken(*keyword, start, line, column);
		return;
	}
	addToken(TokenType::Identifier, start, line, column);
}

void Lexer::lexAsciiSymbol() {
	size_t start = pos_;
	int line = line_;
	int column = column_;
	unsigned char c = peek();

	auto single = [&](TokenType type) {
		advance();
		addToken(type, start, line, column);
	};
	auto twoChar = [&](TokenType type) {
		advanceBytes(2);
		addToken(type, start, line, column);
	};

	switch (c) {
	case '+': single(TokenType::Plus); return;
	case '-': single(TokenType::Minus); return;
	case '*': single(TokenType::Star); return;
	case '/': single(TokenType::Slash); return;
	case '&': single(TokenType::Ampersand); return;
	case '=': single(TokenType::Equal); return;
	case '^': single(TokenType::Caret); return;
	case '(': single(TokenType::LeftParen); return;
	case ')': single(TokenType::RightParen); return;
	case '[': single(TokenType::LeftBracket); return;
	case ']': single(TokenType::RightBracket); return;
	case ',': single(TokenType::Comma); return;
	case ':': single(TokenType::Colon); return;
	case '.': single(TokenType::Dot); return;
	case '<':
		if (peek(1) == '-') twoChar(TokenType::Assign);
		else if (peek(1) == '>') twoChar(TokenType::NotEqual);
		else if (peek(1) == '=') twoChar(TokenType::LessEqual);
		else single(TokenType::Less);
		return;
	case '>':
		if (peek(1) == '=') twoChar(TokenType::GreaterEqual);
		else single(TokenType::Greater);
		return;
	case '\'':
		lexCharLiteral(1);
		return;
	case '"':
		lexStringLiteral(1);
		return;
	case '!':
		if (peek(1) == '=') {
			error("L002", line, column, "'!=' is not an operator. Use <> for 'not equal to'");
			twoChar(TokenType::NotEqual);
			return;
		}
		break;
	case ';':
		advance();
		error("L002", line, column, "Semicolons are not used: a statement ends at the end of its line");
		return;
	case '%':
		advance();
		error("L002", line, column, "'%' is not an operator. Use MOD for the remainder");
		addToken(TokenType::Mod, start, line, column);
		return;
	}

	advance();
	if (c >= 0x20 && c < 0x7F) {
		error("L002", line, column, std::string("Invalid character '") + static_cast<char>(c) + "' in source");
	}
	else {
		error("L002", line, column, "Invalid control character " + codePointName(c) + " in source");
	}
}

void Lexer::lexNonAscii() {
	size_t start = pos_;
	int line = line_;
	int column = column_;
	CodePoint cp = decodeAt(pos_);

	if (!cp.valid) {
		advance();
		error("L002", line, column, "Invalid byte in source: the file is not valid UTF-8 text");
		return;
	}
	if (isCharQuote(cp.value)) {
		lexCharLiteral(cp.length);
		return;
	}
	if (isStringQuote(cp.value)) {
		lexStringLiteral(cp.length);
		return;
	}

	advanceBytes(cp.length);
	switch (cp.value) {
	case LeftwardsArrow:       addToken(TokenType::Assign, start, line, column); return;
	case EnDash:
	case MinusSign:            addToken(TokenType::Minus, start, line, column); return;
	case NotEqualTo:           addToken(TokenType::NotEqual, start, line, column); return;
	case LessThanOrEqualTo:    addToken(TokenType::LessEqual, start, line, column); return;
	case GreaterThanOrEqualTo: addToken(TokenType::GreaterEqual, start, line, column); return;
	case NoBreakSpace:
	case ByteOrderMark:
		return;
	}

	std::string character(source_.substr(start, cp.length));
	std::string message = "Invalid character '" + character + "' (" + codePointName(cp.value) + ") in source";
	if (cp.value == MultiplicationSign) {
		message += ". Use * to multiply";
	}
	else if (cp.value == DivisionSign) {
		message += ". Use / to divide";
	}
	error("L002", line, column, std::move(message));
}

void Lexer::lexCharLiteral(int openerLength) {
	size_t start = pos_;
	int line = line_;
	int column = column_;
	advanceBytes(openerLength);

	size_t contentStart = pos_;
	int count = 0;
	char32_t first = 0;
	while (true) {
		if (atEnd() || peek() == '\n' || peek() == '\r') {
			error("L001", line, column, "This character literal is missing its closing quote '");
			char value = (count >= 1 && first < 0x80) ? static_cast<char>(first) : '\0';
			addToken(TokenType::CharLiteral, start, line, column, value);
			return;
		}
		CodePoint cp = decodeAt(pos_);
		if (isCharQuote(cp.value)) {
			break;
		}
		if (count == 0) {
			first = cp.value;
		}
		++count;
		advanceBytes(cp.length);
	}
	std::string content(source_.substr(contentStart, pos_ - contentStart));
	advanceBytes(decodeAt(pos_).length);

	if (count == 1 && first < 0x80) {
		addToken(TokenType::CharLiteral, start, line, column, static_cast<char>(first));
		return;
	}
	if (count == 0) {
		error("L001", line, column, "Empty character literal: a CHAR must hold exactly one character. Use \"\" for an empty STRING");
		addToken(TokenType::CharLiteral, start, line, column, '\0');
		return;
	}
	if (count == 1) {
		error("L002", line, column, "'" + content + "' is not an ASCII character, so it cannot be a CHAR");
		addToken(TokenType::CharLiteral, start, line, column, '\0');
		return;
	}
	// Probably meant as a string, so carry on with one to avoid follow-on errors.
	error("L001", line, column,
		"A CHAR holds exactly one character, but '" + content + "' has " + std::to_string(count)
		+ ". Use double quotes for a STRING: \"" + content + "\"");
	addToken(TokenType::StringLiteral, start, line, column, content);
}

void Lexer::lexStringLiteral(int openerLength) {
	size_t start = pos_;
	int line = line_;
	int column = column_;
	advanceBytes(openerLength);

	size_t contentStart = pos_;
	while (true) {
		if (atEnd() || peek() == '\n' || peek() == '\r') {
			error("L001", line, column, "This string is missing its closing quote \". A string must end on the line it starts");
			addToken(TokenType::StringLiteral, start, line, column, std::string(source_.substr(contentStart, pos_ - contentStart)));
			return;
		}
		CodePoint cp = decodeAt(pos_);
		if (isStringQuote(cp.value)) {
			break;
		}
		advanceBytes(cp.length);
	}
	std::string content(source_.substr(contentStart, pos_ - contentStart));
	advanceBytes(decodeAt(pos_).length);
	addToken(TokenType::StringLiteral, start, line, column, std::move(content));
}

void Lexer::addToken(TokenType type, size_t start, int line, int column, LiteralValue value) {
	Token token;
	token.type = type;
	token.lexeme = std::string(source_.substr(start, pos_ - start));
	token.line = line;
	token.column = column;
	token.value = std::move(value);
	emit(std::move(token));
}

// Applies the line-structure rules of spec 2.6 as tokens are produced. A
// NEWLINE is held as the last token; if the next token cannot start a
// statement it is removed again.
void Lexer::emit(Token token) {
	if (token.type == TokenType::Newline) {
		if (bracketDepth_ > 0
			|| tokens_.empty()
			|| tokens_.back().type == TokenType::Newline
			|| cannotEndStatement(tokens_.back().type)) {
			return;
		}
		tokens_.push_back(std::move(token));
		return;
	}

	if (!tokens_.empty() && tokens_.back().type == TokenType::Newline && cannotStartStatement(token.type)) {
		tokens_.pop_back();
	}

	if (token.type == TokenType::LeftParen || token.type == TokenType::LeftBracket) {
		++bracketDepth_;
	}
	else if ((token.type == TokenType::RightParen || token.type == TokenType::RightBracket) && bracketDepth_ > 0) {
		--bracketDepth_;
	}
	tokens_.push_back(std::move(token));
}

void Lexer::error(const char* code, int line, int column, std::string message) {
	Diagnostic diagnostic;
	diagnostic.severity = Severity::Error;
	diagnostic.phase = Phase::Lexical;
	diagnostic.code = code;
	diagnostic.line = line;
	diagnostic.column = column;
	diagnostic.message = std::move(message);
	diagnostics_.push_back(std::move(diagnostic));
}
