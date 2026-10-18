#include "Parser.h"

#include <algorithm>
#include <utility>

namespace {

std::string toUpper(std::string_view text) {
	std::string result(text);
	for (char& c : result) {
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}
	return result;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
	return toUpper(a) == toUpper(b);
}

SourceLocation locationOf(const Token& token) {
	return { token.line, token.column };
}

std::string keyword(TokenType type) {
	return std::string(tokenTypeName(type));
}

// Spec 3.2: a block stops, without consuming, at any of these.
bool isBlockTerminator(TokenType type) {
	switch (type) {
	case TokenType::Else: case TokenType::EndIf:
	case TokenType::Otherwise: case TokenType::EndCase:
	case TokenType::Next: case TokenType::Until: case TokenType::EndWhile:
	case TokenType::EndProcedure: case TokenType::EndFunction:
	case TokenType::EndType: case TokenType::EndClass:
		return true;
	default:
		return false;
	}
}

// Keywords that start a definition, which may only appear at top level.
bool isDefinitionStart(TokenType type) {
	switch (type) {
	case TokenType::Type: case TokenType::Procedure: case TokenType::Function:
	case TokenType::Class: case TokenType::Public: case TokenType::Private:
		return true;
	default:
		return false;
	}
}

// The keyword that opens the construct a terminator belongs to.
TokenType openerOf(TokenType terminator) {
	switch (terminator) {
	case TokenType::Else: case TokenType::EndIf:         return TokenType::If;
	case TokenType::Otherwise: case TokenType::EndCase:  return TokenType::Case;
	case TokenType::Next:                                return TokenType::For;
	case TokenType::Until:                               return TokenType::Repeat;
	case TokenType::EndWhile:                            return TokenType::While;
	case TokenType::EndProcedure:                        return TokenType::Procedure;
	case TokenType::EndFunction:                         return TokenType::Function;
	case TokenType::EndType:                             return TokenType::Type;
	case TokenType::EndClass:                            return TokenType::Class;
	default:                                             return terminator;
	}
}

// The keyword that closes a construct.
TokenType closerOf(TokenType opener) {
	switch (opener) {
	case TokenType::If:        return TokenType::EndIf;
	case TokenType::Case:      return TokenType::EndCase;
	case TokenType::For:       return TokenType::Next;
	case TokenType::Repeat:    return TokenType::Until;
	case TokenType::While:     return TokenType::EndWhile;
	case TokenType::Procedure: return TokenType::EndProcedure;
	case TokenType::Function:  return TokenType::EndFunction;
	case TokenType::Type:      return TokenType::EndType;
	case TokenType::Class:     return TokenType::EndClass;
	default:                   return opener;
	}
}

// How a token is named in "but found ..." messages.
std::string describe(const Token& token) {
	switch (token.type) {
	case TokenType::Newline:        return "the end of the line";
	case TokenType::EndOfFile:      return "the end of the file";
	case TokenType::Identifier:     return "'" + token.lexeme + "'";
	case TokenType::IntegerLiteral:
	case TokenType::RealLiteral:
	case TokenType::CharLiteral:
	case TokenType::StringLiteral:
	case TokenType::DateLiteral:    return token.lexeme;
	default:
		if (isKeyword(token.type)) {
			return keyword(token.type);
		}
		return "'" + token.lexeme + "'";
	}
}

std::optional<Literal> literalOf(const Token& token) {
	switch (token.type) {
	case TokenType::True:  return Literal(true);
	case TokenType::False: return Literal(false);
	case TokenType::IntegerLiteral:
		if (auto value = std::get_if<std::int64_t>(&token.value)) return Literal(*value);
		return Literal(std::int64_t{ 0 });
	case TokenType::RealLiteral:
		if (auto value = std::get_if<double>(&token.value)) return Literal(*value);
		return Literal(0.0);
	case TokenType::CharLiteral:
		if (auto value = std::get_if<char>(&token.value)) return Literal(*value);
		return Literal('\0');
	case TokenType::StringLiteral:
		if (auto value = std::get_if<std::string>(&token.value)) return Literal(*value);
		return Literal(std::string());
	case TokenType::DateLiteral:
		if (auto value = std::get_if<Date>(&token.value)) return Literal(*value);
		return Literal(Date{});
	default:
		return std::nullopt;
	}
}

std::optional<BinaryOp> comparisonOperator(const Token& token) {
	switch (token.type) {
	case TokenType::Equal:        return BinaryOp::Equal;
	case TokenType::NotEqual:     return BinaryOp::NotEqual;
	case TokenType::Less:         return BinaryOp::Less;
	case TokenType::Greater:      return BinaryOp::Greater;
	case TokenType::LessEqual:    return BinaryOp::LessEqual;
	case TokenType::GreaterEqual: return BinaryOp::GreaterEqual;
	case TokenType::In:           return BinaryOp::In;
	case TokenType::Assign:
		// Spec 2.4: "X<-1" where an operator is expected means X < -1.
		if (token.lexeme == "<-") return BinaryOp::Less;
		return std::nullopt;
	default:
		return std::nullopt;
	}
}

// Words students often write from other languages, with what to use instead.
std::optional<std::string> statementHint(std::string_view word) {
	std::string upper = toUpper(word);
	if (upper == "PRINT")     return "Use OUTPUT to display values, as in OUTPUT \"Hello\"";
	if (upper == "ELSEIF")    return "There is no ELSEIF. Write ELSE, then the IF on the next line with its own ENDIF";
	if (upper == "ENDFOR")    return "A FOR loop ends with NEXT, as in NEXT Index";
	if (upper == "ENDREPEAT") return "A REPEAT loop ends with UNTIL and a condition";
	if (upper == "END")       return "Use the END keyword for the block, such as ENDIF, ENDWHILE or ENDPROCEDURE";
	return std::nullopt;
}

bool isLValue(const Expr& expr) {
	return std::visit(Overloaded{
		[](const NameExpr&) { return true; },
		[](const IndexExpr& node) { return node.array && isLValue(*node.array); },
		[](const MemberExpr& node) { return node.object && isLValue(*node.object); },
		[](const DerefExpr& node) { return node.pointer && isLValue(*node.pointer); },
		[](const auto&) { return false; },
	}, expr.node);
}

template <class Node>
ExprPtr makeExpr(SourceLocation location, Node node) {
	auto expr = std::make_unique<Expr>();
	expr->location = location;
	expr->node = std::move(node);
	return expr;
}

template <class Node>
StmtPtr makeStmt(SourceLocation location, Node node) {
	auto stmt = std::make_unique<Stmt>();
	stmt->location = location;
	stmt->node = std::move(node);
	return stmt;
}

ExprPtr makeBinary(const Token& op, BinaryOp kind, ExprPtr left, ExprPtr right) {
	return makeExpr(locationOf(op), BinaryExpr{ kind, std::move(left), std::move(right) });
}

}

// ---------- setup ----------

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
	if (tokens_.empty() || tokens_.back().type != TokenType::EndOfFile) {
		Token end;
		end.type = TokenType::EndOfFile;
		if (!tokens_.empty()) {
			end.line = tokens_.back().line;
			end.column = tokens_.back().column;
		}
		tokens_.push_back(std::move(end));
	}
}

bool Parser::hasErrors() const {
	for (const Diagnostic& diagnostic : diagnostics_) {
		if (diagnostic.severity == Severity::Error) {
			return true;
		}
	}
	return false;
}

Parser::OpenScope::OpenScope(Parser& parser, TokenType opener, const Token& at) : parser_(parser) {
	parser_.openStack_.push_back({ opener, at.line, at.column });
}

Parser::OpenScope::~OpenScope() {
	parser_.openStack_.pop_back();
}

// ---------- top level and definitions ----------

// program = { NEWLINE } { top_item } EOF
Program Parser::parse() {
	Program program;
	while (true) {
		skipNewlines();
		const Token& token = current();
		size_t start = pos_;

		switch (token.type) {
		case TokenType::EndOfFile:
			return program;
		case TokenType::Type:
			if (auto typeDef = parseTypeDef()) {
				program.types.push_back(std::move(*typeDef));
			}
			break;
		case TokenType::Procedure:
		case TokenType::Function:
			program.subroutines.push_back(parseSubroutine(Visibility::Public, false));
			break;
		case TokenType::Class:
			program.classes.push_back(parseClass());
			break;
		default:
			if (isBlockTerminator(token.type)) {
				TokenType owner = openerOf(token.type);
				bool belongs = token.type == TokenType::Else || token.type == TokenType::Otherwise;
				error("S002", token, keyword(token.type) + " found, but there is no " + keyword(owner)
					+ (belongs ? " for it to belong to" : " for it to close"));
				skipLine();
			}
			else if (StmtPtr stmt = parseStatementSafely()) {
				program.statements.push_back(std::move(stmt));
			}
			break;
		}

		if (pos_ == start) {
			advance(); // never loop on a token nothing could consume
		}
	}
}

// type_def = "TYPE" ident "=" "(" ident { "," ident } ")"
//          | "TYPE" ident "=" "^" type_spec
//          | "TYPE" ident "=" "SET" "OF" type_spec
//          | "TYPE" ident NEWLINE { declare } "ENDTYPE"
std::optional<TypeDef> Parser::parseTypeDef() {
	const Token& typeToken = advance();
	TypeDef typeDef;
	typeDef.location = locationOf(typeToken);

	try {
		typeDef.name = expectIdentifier("a type name after TYPE");
		if (match(TokenType::Equal)) {
			if (match(TokenType::LeftParen)) {
				EnumTypeDef enumType;
				do {
					enumType.values.push_back(expectIdentifier("a name for each enumerated value"));
				} while (match(TokenType::Comma));
				expect(TokenType::RightParen, "')' after the enumerated values");
				typeDef.body = std::move(enumType);
			}
			else if (match(TokenType::Caret)) {
				typeDef.body = PointerTypeDef{ parseTypeSpec(false) };
			}
			else if (match(TokenType::Set)) {
				expect(TokenType::Of, "OF after SET, as in SET OF CHAR");
				typeDef.body = SetTypeDef{ parseTypeSpec(false) };
			}
			else {
				fail(current(), "Expected '(' for an enumerated type, '^' for a pointer type or SET OF for a set type, but found "
					+ describe(current()));
			}
			expectEndOfLine();
			return typeDef;
		}
		expectEndOfLine();
	}
	catch (const ParseError&) {
		synchronize();
		return std::nullopt;
	}

	// A record type: fields up to ENDTYPE.
	OpenScope scope(*this, TokenType::Type, typeToken);
	RecordTypeDef record;
	while (true) {
		skipNewlines();
		const Token& token = current();
		size_t start = pos_;

		if (token.type == TokenType::EndType) {
			advance();
			expectEndOfLine();
			break;
		}
		if (token.type == TokenType::EndOfFile) {
			reportUnclosed(openStack_.back());
			break;
		}
		if (isBlockTerminator(token.type)) {
			if (handleStrayTerminator(false)) break;
			continue;
		}
		if (isDefinitionStart(token.type)) {
			reportDefinitionInside(token);
			break;
		}

		try {
			if (!check(TokenType::Declare)) {
				if (token.type == TokenType::Identifier && peek(1).type == TokenType::Colon) {
					fail(token, "Fields of a record TYPE are declared with DECLARE, as in DECLARE " + token.lexeme + " : STRING");
				}
				fail(token, "Only DECLARE statements can appear between TYPE and ENDTYPE, but found " + describe(token));
			}
			record.fields.push_back(parseDeclare());
		}
		catch (const ParseError&) {
			if (pos_ == start) {
				advance();
			}
			synchronize();
		}
	}
	typeDef.body = std::move(record);
	return typeDef;
}

// procedure = "PROCEDURE" ident [ "(" [ params ] ")" ] NEWLINE block "ENDPROCEDURE"
// function  = "FUNCTION" ident "(" [ params ] ")" "RETURNS" type_spec NEWLINE block "ENDFUNCTION"
Subroutine Parser::parseSubroutine(Visibility visibility, bool inClass) {
	const Token& keywordToken = advance();
	Subroutine subroutine;
	subroutine.location = locationOf(keywordToken);
	subroutine.isFunction = keywordToken.type == TokenType::Function;
	subroutine.visibility = visibility;
	OpenScope scope(*this, keywordToken.type, keywordToken);

	recoverLine([&] {
		if (inClass && !subroutine.isFunction && check(TokenType::New)) {
			const Token& name = advance();
			subroutine.name = { "NEW", locationOf(name) };
		}
		else {
			subroutine.name = expectIdentifier(subroutine.isFunction
				? "a function name after FUNCTION" : "a procedure name after PROCEDURE");
		}

		if (subroutine.isFunction) {
			expect(TokenType::LeftParen, "'(' after the function name. A FUNCTION needs brackets even with no parameters");
			parseParameters(subroutine.parameters);
			expect(TokenType::Returns, "RETURNS and the type of value the function returns");
			subroutine.returnType = parseTypeSpec(false);
		}
		else {
			if (match(TokenType::LeftParen)) {
				parseParameters(subroutine.parameters);
			}
			if (check(TokenType::Returns)) {
				fail(current(), "A PROCEDURE cannot return a value. Use FUNCTION ... RETURNS for a subroutine that returns one");
			}
		}
		expectEndOfLine();
	});

	TokenType closer = subroutine.isFunction ? TokenType::EndFunction : TokenType::EndProcedure;
	parseBody(subroutine.body, { closer });
	if (match(closer)) {
		expectEndOfLine();
	}
	return subroutine;
}

// params = param { "," param } ;  param = [ "BYVAL" | "BYREF" ] ident ":" param_type
// Called after "(", and consumes the closing ")".
void Parser::parseParameters(std::vector<Parameter>& parameters) {
	if (match(TokenType::RightParen)) {
		return;
	}
	PassMode mode = PassMode::ByValue;
	do {
		if (match(TokenType::ByVal)) {
			mode = PassMode::ByValue;
		}
		else if (match(TokenType::ByRef)) {
			mode = PassMode::ByReference;
		}
		Parameter parameter;
		parameter.mode = mode;
		parameter.name = expectIdentifier("a parameter name");
		expect(TokenType::Colon, "':' and a type after the parameter " + parameter.name.name);
		parameter.type = parseTypeSpec(true);
		parameters.push_back(std::move(parameter));
	} while (match(TokenType::Comma));
	expect(TokenType::RightParen, "')' after the parameters");
}

// class_def = "CLASS" ident [ "INHERITS" ident ] NEWLINE { member } "ENDCLASS"
ClassDef Parser::parseClass() {
	const Token& classToken = advance();
	ClassDef classDef;
	classDef.location = locationOf(classToken);
	OpenScope scope(*this, TokenType::Class, classToken);

	recoverLine([&] {
		classDef.name = expectIdentifier("a class name after CLASS");
		if (match(TokenType::Inherits)) {
			classDef.parent = expectIdentifier("the parent class name after INHERITS");
		}
		expectEndOfLine();
	});

	while (true) {
		skipNewlines();
		const Token& token = current();
		size_t start = pos_;

		if (token.type == TokenType::EndClass) {
			advance();
			expectEndOfLine();
			break;
		}
		if (token.type == TokenType::EndOfFile) {
			reportUnclosed(openStack_.back());
			break;
		}
		if (isBlockTerminator(token.type)) {
			if (handleStrayTerminator(false)) break;
			continue;
		}
		if (token.type == TokenType::Class || token.type == TokenType::Type) {
			reportDefinitionInside(token);
			break;
		}

		try {
			parseClassMember(classDef);
		}
		catch (const ParseError&) {
			if (pos_ == start) {
				advance();
			}
			synchronize();
		}
	}
	return classDef;
}

// member = [ "PUBLIC" | "PRIVATE" ] ( ident ":" type_spec NEWLINE | procedure | function )
//        | declare
//        | assignment
void Parser::parseClassMember(ClassDef& classDef) {
	const Token& first = current();
	std::optional<Visibility> visibility;
	if (match(TokenType::Public)) {
		visibility = Visibility::Public;
	}
	else if (match(TokenType::Private)) {
		visibility = Visibility::Private;
	}

	if (check(TokenType::Procedure) || check(TokenType::Function)) {
		classDef.methods.push_back(parseSubroutine(visibility.value_or(Visibility::Public), true));
		return;
	}
	if (check(TokenType::Identifier) && peek(1).type == TokenType::Colon) {
		Attribute attribute;
		attribute.visibility = visibility.value_or(Visibility::Public);
		attribute.name = expectIdentifier("an attribute name");
		advance(); // ':'
		attribute.type = parseTypeSpec(false);
		expectEndOfLine();
		classDef.attributes.push_back(std::move(attribute));
		return;
	}
	if (visibility) {
		fail(current(), "Expected an attribute, PROCEDURE or FUNCTION after " + keyword(first.type)
			+ ", but found " + describe(current()));
	}
	if (check(TokenType::Declare)) {
		DeclareStmt declare = parseDeclare();
		classDef.attributes.push_back({ std::move(declare.name), std::move(declare.type), Visibility::Public });
		return;
	}
	if (check(TokenType::Identifier)) {
		StmtPtr stmt = parseExpressionStatement();
		if (std::holds_alternative<AssignStmt>(stmt->node)) {
			classDef.initialisers.push_back(std::move(stmt));
		}
		else {
			error("S001", first, "Only attributes, methods and assignments that give an attribute its starting value can appear directly inside a CLASS");
		}
		return;
	}
	fail(first, "Expected an attribute, PROCEDURE or FUNCTION inside the CLASS, but found " + describe(first));
}

// type_spec  = "INTEGER" | "REAL" | "CHAR" | "STRING" | "BOOLEAN" | "DATE" | ident
//            | "ARRAY" "[" bounds { "," bounds } "]" "OF" type_spec
// param_type = type_spec | "ARRAY" "OF" type_spec
TypeSpec Parser::parseTypeSpec(bool allowOpenArray) {
	const Token& token = current();
	TypeSpec spec;
	spec.location = locationOf(token);

	auto builtin = [&](BuiltinType type) {
		advance();
		spec.node = type;
		return std::move(spec);
	};

	switch (token.type) {
	case TokenType::Integer: return builtin(BuiltinType::Integer);
	case TokenType::Real:    return builtin(BuiltinType::Real);
	case TokenType::Char:    return builtin(BuiltinType::Char);
	case TokenType::String:  return builtin(BuiltinType::String);
	case TokenType::Boolean: return builtin(BuiltinType::Boolean);
	case TokenType::Date:    return builtin(BuiltinType::Date);
	case TokenType::Identifier:
		advance();
		spec.node = NamedType{ token.lexeme };
		return spec;
	case TokenType::Array: {
		advance();
		ArrayType array;
		if (check(TokenType::Of)) {
			if (!allowOpenArray) {
				fail(current(), "An ARRAY needs its bounds here, as in ARRAY[1:10] OF INTEGER");
			}
			advance();
		}
		else {
			expect(TokenType::LeftBracket, "'[' and the bounds after ARRAY, as in ARRAY[1:10] OF INTEGER");
			do {
				ArrayBounds bounds;
				bounds.lower = parseExpression();
				expect(TokenType::Colon, "':' between the lower and upper bound, as in [1:10]");
				bounds.upper = parseExpression();
				array.bounds.push_back(std::move(bounds));
			} while (match(TokenType::Comma));
			expect(TokenType::RightBracket, "']' after the array bounds");
			expect(TokenType::Of, "OF and the element type after the array bounds");
		}
		array.element = std::make_unique<TypeSpec>(parseTypeSpec(false));
		spec.node = std::move(array);
		return spec;
	}
	default:
		failExpected("a type such as INTEGER, STRING or ARRAY[1:10] OF INTEGER");
	}
}

// ---------- blocks and statements ----------

// block = { stmt }, ending at one of `closers`, or (inside a CASE) at the next
// label line. Anything else that ends a block is an error reported here
// (spec 3.2), after which the construct is Abandoned.
Parser::BodyEnd Parser::parseBody(Block& into, std::initializer_list<TokenType> closers, bool inCase) {
	while (true) {
		skipNewlines();
		const Token& token = current();
		size_t start = pos_;

		if (std::find(closers.begin(), closers.end(), token.type) != closers.end()) {
			return BodyEnd::AtCloser;
		}
		if (inCase && isCaseLabelLine()) {
			return BodyEnd::AtCloser;
		}
		if (token.type == TokenType::EndOfFile) {
			reportUnclosed(openStack_.back());
			return BodyEnd::Abandoned;
		}
		if (isBlockTerminator(token.type)) {
			if (handleStrayTerminator(true)) {
				return BodyEnd::Abandoned;
			}
			continue;
		}
		if (isDefinitionStart(token.type) && insideDefinition()) {
			// Most likely the END of the current subroutine is missing.
			reportDefinitionInside(token);
			return BodyEnd::Abandoned;
		}

		if (StmtPtr stmt = parseStatementSafely()) {
			into.push_back(std::move(stmt));
		}
		if (pos_ == start) {
			advance();
		}
	}
}

StmtPtr Parser::parseStatementSafely() {
	size_t start = pos_;
	try {
		return parseStatement();
	}
	catch (const ParseError&) {
		// synchronize() stops at a token that starts a line, so step past
		// the statement's own first token when the error was right there.
		if (pos_ == start) {
			advance();
		}
		synchronize();
		return nullptr;
	}
}

StmtPtr Parser::parseStatement() {
	const Token& token = current();
	SourceLocation location = locationOf(token);

	switch (token.type) {
	case TokenType::Declare:    return makeStmt(location, parseDeclare());
	case TokenType::Constant:   return parseConstant();
	case TokenType::Input:      return parseInput();
	case TokenType::Output:     return parseOutput();
	case TokenType::Call:       return parseCall();
	case TokenType::If:         return parseIf(false);
	case TokenType::Case:       return parseCase();
	case TokenType::For:        return parseFor();
	case TokenType::Repeat:     return parseRepeat();
	case TokenType::While:      return parseWhile();
	case TokenType::Return:     return parseReturn();
	case TokenType::Define:     return parseDefine();
	case TokenType::OpenFile:
	case TokenType::ReadFile:
	case TokenType::WriteFile:
	case TokenType::CloseFile:
	case TokenType::Seek:
	case TokenType::GetRecord:
	case TokenType::PutRecord:  return parseFileStatement();
	case TokenType::Identifier:
	case TokenType::Super:      return parseExpressionStatement();

	case TokenType::Type:
	case TokenType::Procedure:
	case TokenType::Function:
	case TokenType::Class: {
		// Reached only outside any definition, e.g. inside a top-level IF.
		std::string where = "a block";
		if (!openStack_.empty()) {
			where = "the " + keyword(openStack_.back().opener) + " on line " + std::to_string(openStack_.back().line);
		}
		error("S005", token, keyword(token.type) + " definitions must be at the top level of the program, not inside " + where);
		// Parse it anyway so its body and END keyword are not reported as well.
		if (token.type == TokenType::Type) {
			parseTypeDef();
		}
		else if (token.type == TokenType::Class) {
			parseClass();
		}
		else {
			parseSubroutine(Visibility::Public, false);
		}
		return nullptr;
	}
	case TokenType::Public:
	case TokenType::Private:
		error("S001", token, "PUBLIC and PRIVATE can only be used for members of a CLASS");
		advance();
		return nullptr;

	case TokenType::Then:
		fail(token, "THEN must be on the same line as the IF condition, or straight after it");
	case TokenType::Assign:
		fail(token, "A line cannot start with ←. Write the variable first, as in Total ← 0");
	default:
		fail(token, "Expected a statement, but found " + describe(token));
	}
}

// declare = "DECLARE" ident ":" type_spec NEWLINE
DeclareStmt Parser::parseDeclare() {
	advance(); // DECLARE
	DeclareStmt declare;
	declare.name = expectIdentifier("a variable name after DECLARE");
	if (check(TokenType::Comma)) {
		fail(current(), "Declare one variable per DECLARE statement");
	}
	expect(TokenType::Colon, "':' and a type after " + declare.name.name);
	declare.type = parseTypeSpec(false);
	expectEndOfLine();
	return declare;
}

// constant = "CONSTANT" ident "=" [ "-" ] literal NEWLINE
StmtPtr Parser::parseConstant() {
	const Token& constantToken = advance();
	ConstantStmt constant;
	constant.name = expectIdentifier("a name after CONSTANT");
	if (check(TokenType::Assign)) {
		fail(current(), "A CONSTANT is given its value with =, as in CONSTANT " + constant.name.name + " = 10");
	}
	expect(TokenType::Equal, "'=' after the constant name");
	constant.value = parseLiteralValue("a literal value such as 10, 3.5 or \"Hello\" (a CONSTANT cannot be set from an expression or another name)");
	expectEndOfLine();
	return makeStmt(locationOf(constantToken), std::move(constant));
}

// assignment = lvalue "←" expression NEWLINE
// call       = callee "(" [ args ] ")" NEWLINE      (method calls without CALL, spec 11.4)
StmtPtr Parser::parseExpressionStatement() {
	const Token& first = current();
	SourceLocation location = locationOf(first);
	ExprPtr target = parsePostfix();

	if (check(TokenType::Assign)) {
		const Token& arrow = advance();
		if (!isLValue(*target)) {
			error("S001", arrow, "The left side of ← must be a variable, array element, field or dereferenced pointer");
		}
		ExprPtr value = parseExpression();
		expectEndOfLine();
		return makeStmt(location, AssignStmt{ std::move(target), std::move(value) });
	}

	if (auto* call = std::get_if<CallExpr>(&target->node)) {
		expectEndOfLine();
		return makeStmt(location, CallStmt{ std::move(call->callee), std::move(call->arguments), false });
	}

	if (check(TokenType::Equal)) {
		fail(current(), "Use ← (or <-) to assign a value. = compares two values");
	}
	if (auto* name = std::get_if<NameExpr>(&target->node)) {
		if (auto hint = statementHint(name->name)) {
			fail(first, *hint);
		}
		if (check(TokenType::Newline) || check(TokenType::EndOfFile)) {
			fail(first, "'" + name->name + "' on its own is not a statement. To call a procedure, write CALL " + name->name);
		}
	}
	failExpected("← to assign a value");
}

// input = "INPUT" lvalue NEWLINE
StmtPtr Parser::parseInput() {
	const Token& inputToken = advance();
	ExprPtr target = parseLValue("INPUT needs a variable to store the value in");
	expectEndOfLine();
	return makeStmt(locationOf(inputToken), InputStmt{ std::move(target) });
}

// output = "OUTPUT" expression { "," expression } NEWLINE
StmtPtr Parser::parseOutput() {
	const Token& outputToken = advance();
	OutputStmt output;
	do {
		output.values.push_back(parseExpression());
	} while (match(TokenType::Comma));
	expectEndOfLine();
	return makeStmt(locationOf(outputToken), std::move(output));
}

// call   = "CALL" callee [ "(" [ args ] ")" ] NEWLINE
// callee = ident | ident "." ident | "SUPER" "." ident
StmtPtr Parser::parseCall() {
	const Token& callToken = advance();
	CallStmt call;

	if (check(TokenType::Super)) {
		const Token& super = advance();
		expect(TokenType::Dot, "'.' and a method name after SUPER");
		call.callee = makeExpr(locationOf(super), SuperExpr{ parseMemberName(true) });
	}
	else {
		const Token& nameToken = current();
		Identifier name = expectIdentifier("a procedure name after CALL");
		call.callee = makeExpr(locationOf(nameToken), NameExpr{ std::move(name.name) });
		if (check(TokenType::Dot)) {
			const Token& dot = advance();
			call.callee = makeExpr(locationOf(dot), MemberExpr{ std::move(call.callee), parseMemberName(false) });
		}
	}

	if (match(TokenType::LeftParen)) {
		if (!check(TokenType::RightParen)) {
			call.arguments = parseArguments();
		}
		expect(TokenType::RightParen, "')' after the arguments");
	}
	expectEndOfLine();
	return makeStmt(locationOf(callToken), std::move(call));
}

// if_stmt = "IF" expression "THEN" NEWLINE block [ "ELSE" NEWLINE block ] "ENDIF" NEWLINE
//
// `chainedElseIf` is set for an IF written straight after ELSE on the same
// line. That is an error (spec 3.2), but parsing it as a chain sharing the
// outer ENDIF avoids a second error about a missing ENDIF.
StmtPtr Parser::parseIf(bool chainedElseIf) {
	const Token& ifToken = advance();
	OpenScope scope(*this, TokenType::If, ifToken);
	IfStmt node;

	recoverLine([&] {
		node.condition = parseExpression();
		expect(TokenType::Then, "THEN after the IF condition");
		expectEndOfLine();
	});

	parseBody(node.thenBranch, { TokenType::Else, TokenType::EndIf });
	if (check(TokenType::Else)) {
		const Token& elseToken = advance();
		if (check(TokenType::If) && current().line == elseToken.line) {
			error("S001", current(), "There is no ELSE IF. Put the IF on a new line after ELSE, and close it with its own ENDIF");
			node.elseBranch.push_back(parseIf(true));
		}
		else {
			expectEndOfLine();
			parseBody(node.elseBranch, { TokenType::EndIf });
		}
	}
	if (!chainedElseIf && match(TokenType::EndIf)) {
		expectEndOfLine();
	}
	return makeStmt(locationOf(ifToken), std::move(node));
}

// case_stmt = "CASE" "OF" expression NEWLINE { case_clause }
//             [ "OTHERWISE" [ ":" ] clause_body ] "ENDCASE" NEWLINE
StmtPtr Parser::parseCase() {
	const Token& caseToken = advance();
	OpenScope scope(*this, TokenType::Case, caseToken);
	CaseStmt node;

	recoverLine([&] {
		expect(TokenType::Of, "OF after CASE, as in CASE OF Choice");
		node.selector = parseExpression();
		expectEndOfLine();
	});

	while (true) {
		skipNewlines();
		const Token& token = current();

		if (token.type == TokenType::EndCase) {
			advance();
			expectEndOfLine();
			break;
		}

		if (token.type == TokenType::Otherwise) {
			advance();
			match(TokenType::Colon);
			Block body;
			BodyEnd end = parseClauseBody(body);
			if (node.otherwise) {
				error("S004", token, "This CASE already has an OTHERWISE clause");
			}
			else {
				node.otherwise = std::move(body);
			}
			if (end == BodyEnd::Abandoned) break;
			continue;
		}

		if (isCaseLabelLine()) {
			if (node.otherwise) {
				error("S004", token, "OTHERWISE must be the last clause in a CASE. Move this clause above it");
			}
			CaseClause clause;
			clause.location = locationOf(token);
			try {
				do {
					clause.labels.push_back(parseCaseLabel());
				} while (match(TokenType::Comma));
				expect(TokenType::Colon, "':' after the CASE label");
			}
			catch (const ParseError&) {
				// The line is known to contain ':', so skip the labels up to
				// it and still parse the clause's statements.
				while (!check(TokenType::Colon) && !check(TokenType::Newline) && !check(TokenType::EndOfFile)) {
					advance();
				}
				match(TokenType::Colon);
			}
			BodyEnd end = parseClauseBody(clause.body);
			node.clauses.push_back(std::move(clause));
			if (end == BodyEnd::Abandoned) break;
			continue;
		}

		// Statements before the first label, or an error that ends the CASE.
		Block stray;
		BodyEnd end = parseBody(stray, { TokenType::EndCase, TokenType::Otherwise }, true);
		if (!stray.empty()) {
			error("S001", stray.front()->location.line, stray.front()->location.column,
				"Each statement in a CASE must follow a label, such as 'A' : or OTHERWISE :");
		}
		if (end == BodyEnd::Abandoned) break;
	}
	return makeStmt(locationOf(caseToken), std::move(node));
}

// clause_body = [ stmt_inline ] NEWLINE block
// The block ends at the next label line, OTHERWISE or ENDCASE.
Parser::BodyEnd Parser::parseClauseBody(Block& into) {
	if (!check(TokenType::Newline) && !check(TokenType::EndOfFile)
		&& !isBlockTerminator(current().type) && !isCaseLabelLine()) {
		if (StmtPtr stmt = parseStatementSafely()) {
			into.push_back(std::move(stmt));
		}
	}
	return parseBody(into, { TokenType::EndCase, TokenType::Otherwise }, true);
}

// case_label = label_value [ "TO" label_value ]
CaseLabel Parser::parseCaseLabel() {
	CaseLabel label;
	label.value = parseLabelValue();
	if (match(TokenType::To)) {
		label.upTo = parseLabelValue();
	}
	return label;
}

// label_value = [ "-" ] literal | ident
ExprPtr Parser::parseLabelValue() {
	const Token& token = current();
	if (token.type == TokenType::Identifier) {
		advance();
		return makeExpr(locationOf(token), NameExpr{ token.lexeme });
	}
	Literal value = parseLiteralValue("a CASE label: a literal value, a constant or an enumerated value");
	return makeExpr(locationOf(token), LiteralExpr{ std::move(value) });
}

// for_stmt = "FOR" ident "←" expression "TO" expression [ "STEP" expression ] NEWLINE
//            block "NEXT" [ ident ] NEWLINE
StmtPtr Parser::parseFor() {
	const Token& forToken = advance();
	OpenScope scope(*this, TokenType::For, forToken);
	ForStmt node;

	recoverLine([&] {
		node.variable = expectIdentifier("the loop variable after FOR");
		if (check(TokenType::Equal)) {
			fail(current(), "Use ← (or <-) to give the loop variable its first value, as in FOR "
				+ node.variable.name + " ← 1 TO 10");
		}
		expect(TokenType::Assign, "← and a start value after the loop variable");
		node.start = parseExpression();
		expect(TokenType::To, "TO and an end value after the start value");
		node.end = parseExpression();
		if (match(TokenType::Step)) {
			node.step = parseExpression();
		}
		expectEndOfLine();
	});

	parseBody(node.body, { TokenType::Next });
	if (match(TokenType::Next)) {
		if (check(TokenType::Identifier)) {
			const Token& name = advance();
			if (!node.variable.name.empty() && !equalsIgnoreCase(name.lexeme, node.variable.name)) {
				error("S003", name, "NEXT " + name.lexeme + " does not match the loop variable " + node.variable.name
					+ " of the FOR on line " + std::to_string(forToken.line));
			}
		}
		expectEndOfLine();
	}
	return makeStmt(locationOf(forToken), std::move(node));
}

// repeat_stmt = "REPEAT" NEWLINE block "UNTIL" expression NEWLINE
StmtPtr Parser::parseRepeat() {
	const Token& repeatToken = advance();
	OpenScope scope(*this, TokenType::Repeat, repeatToken);
	RepeatStmt node;

	expectEndOfLine();
	parseBody(node.body, { TokenType::Until });
	if (match(TokenType::Until)) {
		recoverLine([&] {
			node.condition = parseExpression();
			expectEndOfLine();
		});
	}
	return makeStmt(locationOf(repeatToken), std::move(node));
}

// while_stmt = "WHILE" expression [ "DO" ] NEWLINE block "ENDWHILE" NEWLINE
StmtPtr Parser::parseWhile() {
	const Token& whileToken = advance();
	OpenScope scope(*this, TokenType::While, whileToken);
	WhileStmt node;

	recoverLine([&] {
		node.condition = parseExpression();
		// DO is a contextual keyword accepted for older papers (spec 3.2).
		if (check(TokenType::Identifier) && current().lexeme == "DO") {
			advance();
		}
		expectEndOfLine();
	});

	parseBody(node.body, { TokenType::EndWhile });
	if (match(TokenType::EndWhile)) {
		expectEndOfLine();
	}
	return makeStmt(locationOf(whileToken), std::move(node));
}

// return_stmt = "RETURN" expression NEWLINE
StmtPtr Parser::parseReturn() {
	const Token& returnToken = advance();
	if (check(TokenType::Newline) || check(TokenType::EndOfFile)) {
		fail(returnToken, "RETURN needs the value to return. A PROCEDURE does not use RETURN: it ends at ENDPROCEDURE");
	}
	ExprPtr value = parseExpression();
	expectEndOfLine();
	return makeStmt(locationOf(returnToken), ReturnStmt{ std::move(value) });
}

// file_stmt (spec 3.1, 10)
StmtPtr Parser::parseFileStatement() {
	const Token& keywordToken = advance();
	SourceLocation location = locationOf(keywordToken);
	std::string name = keyword(keywordToken.type);
	ExprPtr file = parseExpression();
	StmtPtr stmt;

	switch (keywordToken.type) {
	case TokenType::OpenFile: {
		expect(TokenType::For, "FOR and a file mode after the file name, as in OPENFILE \"Data.txt\" FOR READ");
		FileMode mode;
		switch (current().type) {
		case TokenType::Read:   mode = FileMode::Read; break;
		case TokenType::Write:  mode = FileMode::Write; break;
		case TokenType::Append: mode = FileMode::Append; break;
		case TokenType::Random: mode = FileMode::Random; break;
		default:
			failExpected("READ, WRITE, APPEND or RANDOM after FOR");
		}
		advance();
		stmt = makeStmt(location, OpenFileStmt{ std::move(file), mode });
		break;
	}
	case TokenType::CloseFile:
		stmt = makeStmt(location, CloseFileStmt{ std::move(file) });
		break;
	case TokenType::ReadFile:
		expect(TokenType::Comma, "',' and a variable after the file name");
		stmt = makeStmt(location, ReadFileStmt{ std::move(file), parseLValue("READFILE needs a variable to store the line in") });
		break;
	case TokenType::WriteFile:
		expect(TokenType::Comma, "',' and the value to write after the file name");
		stmt = makeStmt(location, WriteFileStmt{ std::move(file), parseExpression() });
		break;
	case TokenType::Seek:
		expect(TokenType::Comma, "',' and a record address after the file name");
		stmt = makeStmt(location, SeekStmt{ std::move(file), parseExpression() });
		break;
	case TokenType::GetRecord:
		expect(TokenType::Comma, "',' and a variable after the file name");
		stmt = makeStmt(location, GetRecordStmt{ std::move(file), parseLValue("GETRECORD needs a variable to store the record in") });
		break;
	default: // PutRecord
		expect(TokenType::Comma, "',' and a variable after the file name");
		stmt = makeStmt(location, PutRecordStmt{ std::move(file), parseLValue("PUTRECORD writes a variable") });
		break;
	}
	expectEndOfLine();
	return stmt;
}

// set_define = "DEFINE" ident "(" literal { "," literal } ")" ":" ident NEWLINE
StmtPtr Parser::parseDefine() {
	const Token& defineToken = advance();
	if (!openStack_.empty()) {
		const OpenConstruct& open = openStack_.back();
		error("S005", defineToken, "DEFINE must be at the top level of the program, not inside the "
			+ keyword(open.opener) + " on line " + std::to_string(open.line));
	}
	DefineStmt define;
	define.name = expectIdentifier("a set name after DEFINE");
	expect(TokenType::LeftParen, "'(' and the set's values after its name");
	do {
		define.values.push_back(parseLiteralValue("a literal value in the set"));
	} while (match(TokenType::Comma));
	expect(TokenType::RightParen, "')' after the set's values");
	expect(TokenType::Colon, "':' and the set type after the values");
	define.setType = expectIdentifier("the name of a SET type");
	expectEndOfLine();
	return makeStmt(locationOf(defineToken), std::move(define));
}

// ---------- expressions ----------

ExprPtr Parser::parseExpression() {
	return parseOr();
}

// or_expr = and_expr { "OR" and_expr }
ExprPtr Parser::parseOr() {
	ExprPtr left = parseAnd();
	while (check(TokenType::Or)) {
		const Token& op = advance();
		left = makeBinary(op, BinaryOp::Or, std::move(left), parseAnd());
	}
	return left;
}

// and_expr = not_expr { "AND" not_expr }
ExprPtr Parser::parseAnd() {
	ExprPtr left = parseNot();
	while (check(TokenType::And)) {
		const Token& op = advance();
		left = makeBinary(op, BinaryOp::And, std::move(left), parseNot());
	}
	return left;
}

// not_expr = "NOT" not_expr | comparison
ExprPtr Parser::parseNot() {
	if (check(TokenType::Not)) {
		const Token& op = advance();
		return makeExpr(locationOf(op), UnaryExpr{ UnaryOp::Not, parseNot() });
	}
	return parseComparison();
}

// comparison = concat [ rel_op concat ]     (non-associative)
ExprPtr Parser::parseComparison() {
	ExprPtr left = parseConcat();
	std::optional<BinaryOp> op = comparisonOperator(current());
	if (!op) {
		return left;
	}
	const Token& opToken = advance();
	if (opToken.type == TokenType::Assign) {
		negateNextOperand_ = true;
	}
	left = makeBinary(opToken, *op, std::move(left), parseConcat());
	if (comparisonOperator(current())) {
		fail(current(), "Comparisons cannot be chained. Instead of A < B < C, write A < B AND B < C");
	}
	return left;
}

// concat = additive { "&" additive }
ExprPtr Parser::parseConcat() {
	ExprPtr left = parseAdditive();
	while (check(TokenType::Ampersand)) {
		const Token& op = advance();
		left = makeBinary(op, BinaryOp::Concat, std::move(left), parseAdditive());
	}
	return left;
}

// additive = term { ( "+" | "-" ) term }
ExprPtr Parser::parseAdditive() {
	ExprPtr left = parseTerm();
	while (check(TokenType::Plus) || check(TokenType::Minus)) {
		const Token& op = advance();
		BinaryOp kind = op.type == TokenType::Plus ? BinaryOp::Add : BinaryOp::Subtract;
		left = makeBinary(op, kind, std::move(left), parseTerm());
	}
	return left;
}

// term = unary { ( "*" | "/" | "DIV" | "MOD" ) unary }
ExprPtr Parser::parseTerm() {
	ExprPtr left = parseUnary();
	while (true) {
		BinaryOp kind;
		switch (current().type) {
		case TokenType::Star:  kind = BinaryOp::Multiply; break;
		case TokenType::Slash: kind = BinaryOp::Divide; break;
		case TokenType::Div:   kind = BinaryOp::IntDiv; break;
		case TokenType::Mod:   kind = BinaryOp::Mod; break;
		default:               return left;
		}
		const Token& op = advance();
		left = makeBinary(op, kind, std::move(left), parseUnary());
	}
}

// unary = ( "-" | "+" ) unary | "^" lvalue | postfix
ExprPtr Parser::parseUnary() {
	if (negateNextOperand_) {
		// The "-" of a "<-" that was re-read as "<" (spec 2.4).
		negateNextOperand_ = false;
		const Token& arrow = tokens_[pos_ - 1];
		SourceLocation location{ arrow.line, arrow.column + 1 };
		return makeExpr(location, UnaryExpr{ UnaryOp::Negate, parseUnary() });
	}
	if (check(TokenType::Minus) || check(TokenType::Plus)) {
		const Token& op = advance();
		UnaryOp kind = op.type == TokenType::Minus ? UnaryOp::Negate : UnaryOp::Plus;
		return makeExpr(locationOf(op), UnaryExpr{ kind, parseUnary() });
	}
	if (check(TokenType::Caret)) {
		const Token& op = advance();
		ExprPtr target = parsePostfix();
		if (!isLValue(*target)) {
			fail(op, "^ before a value takes its address, so it must be followed by a variable, array element or field");
		}
		return makeExpr(locationOf(op), AddressOfExpr{ std::move(target) });
	}
	return parsePostfix();
}

// postfix = primary { "[" args "]" | "(" [ args ] ")" | "." ident | "^" }
ExprPtr Parser::parsePostfix() {
	ExprPtr expr = parsePrimary();
	while (true) {
		if (check(TokenType::LeftBracket)) {
			const Token& bracket = advance();
			if (check(TokenType::RightBracket)) {
				fail(current(), "An index is needed between [ and ]");
			}
			std::vector<ExprPtr> indices = parseArguments();
			expect(TokenType::RightBracket, "']' after the array index");
			expr = makeExpr(locationOf(bracket), IndexExpr{ std::move(expr), std::move(indices) });
		}
		else if (check(TokenType::LeftParen)) {
			const Token& paren = advance();
			std::vector<ExprPtr> arguments;
			if (!check(TokenType::RightParen)) {
				arguments = parseArguments();
			}
			expect(TokenType::RightParen, "')' after the arguments");
			expr = makeExpr(locationOf(paren), CallExpr{ std::move(expr), std::move(arguments) });
		}
		else if (check(TokenType::Dot)) {
			const Token& dot = advance();
			expr = makeExpr(locationOf(dot), MemberExpr{ std::move(expr), parseMemberName(false) });
		}
		else if (check(TokenType::Caret)) {
			const Token& caret = advance();
			expr = makeExpr(locationOf(caret), DerefExpr{ std::move(expr) });
		}
		else {
			return expr;
		}
	}
}

// primary = literal | ident | "(" expression ")"
//         | "NEW" ident "(" [ args ] ")" | "SUPER" "." ident
ExprPtr Parser::parsePrimary() {
	const Token& token = current();
	SourceLocation location = locationOf(token);

	if (auto literal = literalOf(token)) {
		advance();
		return makeExpr(location, LiteralExpr{ std::move(*literal) });
	}

	switch (token.type) {
	case TokenType::Identifier:
		advance();
		return makeExpr(location, NameExpr{ token.lexeme });

	case TokenType::LeftParen: {
		advance();
		ExprPtr inner = parseExpression();
		expect(TokenType::RightParen, "')' to close the bracket");
		return inner;
	}

	case TokenType::New: {
		advance();
		Identifier className = expectIdentifier("a class name after NEW");
		expect(TokenType::LeftParen, "'(' after the class name, as in NEW " + className.name + "()");
		std::vector<ExprPtr> arguments;
		if (!check(TokenType::RightParen)) {
			arguments = parseArguments();
		}
		expect(TokenType::RightParen, "')' after the arguments");
		return makeExpr(location, NewExpr{ std::move(className), std::move(arguments) });
	}

	case TokenType::Super:
		advance();
		expect(TokenType::Dot, "'.' and a method name after SUPER");
		return makeExpr(location, SuperExpr{ parseMemberName(true) });

	case TokenType::Mod:
	case TokenType::Div:
		// MOD(A, B) and DIV(A, B) are built-in functions (spec 9.2).
		if (peek(1).type == TokenType::LeftParen) {
			advance();
			return makeExpr(location, NameExpr{ keyword(token.type) });
		}
		break;

	case TokenType::Not:
		fail(token, "NOT here needs brackets around it, as in (NOT Found)");

	default:
		break;
	}
	failExpected("a value");
}

// args = expression { "," expression }
std::vector<ExprPtr> Parser::parseArguments() {
	std::vector<ExprPtr> arguments;
	do {
		arguments.push_back(parseExpression());
	} while (match(TokenType::Comma));
	return arguments;
}

// lvalue = ident { "[" args "]" | "." ident | "^" }
ExprPtr Parser::parseLValue(std::string_view purpose) {
	const Token& first = current();
	if (first.type != TokenType::Identifier) {
		fail(first, std::string(purpose) + ", but found " + describe(first));
	}
	ExprPtr target = parsePostfix();
	if (!isLValue(*target)) {
		fail(first, std::string(purpose) + ". A variable, array element or field is needed here");
	}
	return target;
}

// [ "-" ] literal, as used by CONSTANT, DEFINE and CASE labels.
Literal Parser::parseLiteralValue(std::string_view expected) {
	bool negative = false;
	const Token* minus = nullptr;
	if (check(TokenType::Minus)) {
		minus = &advance();
		negative = true;
	}
	const Token& token = current();
	std::optional<Literal> value = literalOf(token);
	if (!value) {
		failExpected(expected);
	}
	advance();

	if (negative) {
		if (auto* integer = std::get_if<std::int64_t>(&*value)) {
			*integer = -*integer;
		}
		else if (auto* real = std::get_if<double>(&*value)) {
			*real = -*real;
		}
		else {
			fail(*minus, "Only numbers can be negative");
		}
	}
	return std::move(*value);
}

// A name after '.'. NEW is allowed after SUPER, for SUPER.NEW(...).
Identifier Parser::parseMemberName(bool allowNew) {
	if (allowNew && check(TokenType::New)) {
		const Token& token = advance();
		return { "NEW", locationOf(token) };
	}
	return expectIdentifier("a name after '.'");
}

// ---------- token access ----------

const Token& Parser::peek(size_t ahead) const {
	return tokens_[std::min(pos_ + ahead, tokens_.size() - 1)];
}

bool Parser::match(TokenType type) {
	if (check(type)) {
		advance();
		return true;
	}
	return false;
}

const Token& Parser::advance() {
	const Token& token = tokens_[pos_];
	if (token.type != TokenType::EndOfFile) {
		++pos_;
	}
	return token;
}

const Token& Parser::expect(TokenType type, std::string_view expected) {
	if (check(type)) {
		return advance();
	}
	failExpected(expected);
}

Identifier Parser::expectIdentifier(std::string_view expected) {
	const Token& token = current();
	if (token.type != TokenType::Identifier) {
		std::string hint;
		if (isKeyword(token.type) && !atLineStart()) {
			hint = keyword(token.type) + " is a keyword, so it cannot be used as a name";
		}
		failExpected(expected, hint);
	}
	advance();
	return { token.lexeme, locationOf(token) };
}

// Every statement ends at a NEWLINE. Reports anything else left on the line
// and skips it, without throwing, so the statement itself is kept.
//
// A token that starts a later line also counts: the lexer drops NEWLINEs
// inside unclosed brackets (spec 2.6), so after a missing ')' the following
// lines arrive without them. The missing bracket has already been reported.
void Parser::expectEndOfLine() {
	if (match(TokenType::Newline) || check(TokenType::EndOfFile) || atLineStart()) {
		return;
	}
	error("S001", current(), "Expected the end of the line, but found " + describe(current()));
	synchronize();
}

void Parser::skipNewlines() {
	while (match(TokenType::Newline)) {
	}
}

bool Parser::atLineStart() const {
	return pos_ == 0 || tokens_[pos_ - 1].type == TokenType::Newline || tokens_[pos_ - 1].line < current().line;
}

// Spec 3.2: inside a CASE, a line is a label line if it does not start with a
// keyword and contains ':' at bracket depth 0 before its NEWLINE.
bool Parser::isCaseLabelLine() const {
	const Token& first = current();
	if (first.type == TokenType::Newline || first.type == TokenType::EndOfFile) {
		return false;
	}
	if (isKeyword(first.type) && first.type != TokenType::True && first.type != TokenType::False) {
		return false;
	}
	int depth = 0;
	for (size_t i = pos_; i < tokens_.size(); ++i) {
		switch (tokens_[i].type) {
		case TokenType::Newline:
		case TokenType::EndOfFile:
			return false;
		case TokenType::LeftParen:
		case TokenType::LeftBracket:
			++depth;
			break;
		case TokenType::RightParen:
		case TokenType::RightBracket:
			--depth;
			break;
		case TokenType::Colon:
			if (depth == 0) {
				return true;
			}
			break;
		default:
			break;
		}
	}
	return false;
}

// ---------- errors and recovery ----------

void Parser::error(const char* code, int line, int column, std::string message) {
	// One error per line: later ones are usually knock-on effects of the first.
	if (line == lastErrorLine_) {
		return;
	}
	lastErrorLine_ = line;

	Diagnostic diagnostic;
	diagnostic.severity = Severity::Error;
	diagnostic.phase = Phase::Syntax;
	diagnostic.code = code;
	diagnostic.line = line;
	diagnostic.column = column;
	diagnostic.message = std::move(message);
	diagnostics_.push_back(std::move(diagnostic));
}

void Parser::error(const char* code, const Token& at, std::string message) {
	error(code, at.line, at.column, std::move(message));
}

void Parser::fail(const Token& at, std::string message) {
	error("S001", at, std::move(message));
	throw ParseError{};
}

// Reports "Expected <expected>, but found <current token>". If the line ended
// with no NEWLINE token (see expectEndOfLine), the error points just past the
// end of that line rather than at the first token of the next one.
void Parser::failExpected(std::string_view expected, std::string_view hint) {
	std::string message = "Expected " + std::string(expected);
	if (pos_ > 0 && current().type != TokenType::EndOfFile && atLineStart()
		&& tokens_[pos_ - 1].type != TokenType::Newline) {
		const Token& previous = tokens_[pos_ - 1];
		int length = 0;
		for (unsigned char c : previous.lexeme) {
			if ((c & 0xC0) != 0x80) {
				++length;
			}
		}
		error("S001", previous.line, previous.column + length, message + ", but the line ended");
		throw ParseError{};
	}
	message += ", but found " + describe(current());
	if (!hint.empty()) {
		message += ". " + std::string(hint);
	}
	fail(current(), std::move(message));
}

// Skips to the start of the next line. Also stops at a token that starts a
// later line, which happens when the lexer dropped the NEWLINE between them:
// either a line continuation (spec 2.6), so the next line holds a block
// terminator that is still needed, or an unclosed bracket.
void Parser::synchronize() {
	negateNextOperand_ = false;
	while (!check(TokenType::Newline) && !check(TokenType::EndOfFile)) {
		if (atLineStart()) {
			return;
		}
		advance();
	}
	match(TokenType::Newline);
}

// Skips the current token and the rest of its line.
void Parser::skipLine() {
	advance();
	synchronize();
}

// Called when a block meets a terminator that is not one of its own closers.
// Returns true if the construct being parsed is over.
bool Parser::handleStrayTerminator(bool wrongCloserEndsConstruct) {
	const Token& token = current();
	const OpenConstruct& open = openStack_.back();
	TokenType owner = openerOf(token.type);
	std::string found = keyword(token.type) + " found, but the " + keyword(open.opener)
		+ " on line " + std::to_string(open.line) + " is still open";

	// It belongs to an enclosing construct: this one is missing its closer.
	bool ownerIsEnclosing = std::any_of(openStack_.begin(), openStack_.end() - 1,
		[&](const OpenConstruct& c) { return c.opener == owner; });
	if (ownerIsEnclosing) {
		error("S002", token, found + ". Add " + keyword(closerOf(open.opener)) + " before " + keyword(token.type));
		return true;
	}

	// ELSE and OTHERWISE are not closers, so skip them and keep going.
	if (token.type == TokenType::Else || token.type == TokenType::Otherwise) {
		if (owner == open.opener) {
			error("S002", token, "This " + keyword(open.opener) + " on line " + std::to_string(open.line)
				+ " already has " + (token.type == TokenType::Else ? "an ELSE" : "an OTHERWISE"));
		}
		else {
			error("S002", token, keyword(token.type) + " found, but there is no " + keyword(owner) + " for it to belong to");
		}
		skipLine();
		return false;
	}

	if (!wrongCloserEndsConstruct) {
		error("S002", token, keyword(token.type) + " found, but there is no " + keyword(owner) + " for it to close");
		skipLine();
		return false;
	}

	// Most likely the wrong END keyword for this construct, so let it close it.
	error("S002", token, found + ". It should be closed with " + keyword(closerOf(open.opener)));
	skipLine();
	return true;
}

void Parser::reportUnclosed(const OpenConstruct& open) {
	error("S002", open.line, open.column, "This " + keyword(open.opener) + " is never closed. Add "
		+ keyword(closerOf(open.opener)) + " at the end of it");
}

void Parser::reportDefinitionInside(const Token& token) {
	const OpenConstruct& open = openStack_.back();
	error("S002", token, keyword(token.type) + " found, but the " + keyword(open.opener) + " on line "
		+ std::to_string(open.line) + " is still open. Add " + keyword(closerOf(open.opener)) + " before it");
}

bool Parser::insideDefinition() const {
	return std::any_of(openStack_.begin(), openStack_.end(), [](const OpenConstruct& open) {
		return open.opener == TokenType::Procedure || open.opener == TokenType::Function
			|| open.opener == TokenType::Class || open.opener == TokenType::Type;
	});
}
