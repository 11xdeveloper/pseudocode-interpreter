#include "Analyzer.h"

#include <algorithm>

#include "Builtins.h"
#include "Operations.h"
#include "RuntimeError.h"

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

Value literalValue(const Literal& literal) {
	return std::visit([](const auto& value) -> Value { return value; }, literal);
}

std::string subroutineKind(const Subroutine& subroutine) {
	return subroutine.isFunction ? "function" : "procedure";
}

// Expressions that name storage: what BYREF, INPUT and assignment need.
bool isPlace(const Expr& expr) {
	return std::holds_alternative<NameExpr>(expr.node) || std::holds_alternative<IndexExpr>(expr.node)
		|| std::holds_alternative<MemberExpr>(expr.node) || std::holds_alternative<DerefExpr>(expr.node);
}

// How an expression is named in messages: a variable by name, else "This".
std::string nameOf(const Expr& expr) {
	if (const auto* name = std::get_if<NameExpr>(&expr.node)) {
		return name->name;
	}
	return "This";
}

// A CASE label as written: 'A', "Yes", 10, Winter.
std::string labelText(const Value& value) {
	if (const auto* c = std::get_if<char>(&value)) return std::string("'") + *c + "'";
	if (const auto* s = std::get_if<std::string>(&value)) return "\"" + *s + "\"";
	return formatValue(value);
}

// Why a value of this type cannot be output or written to a text file.
std::string notOutputtable(const Type* type) {
	switch (type->kind) {
	case TypeKind::Array:   return "A whole array cannot be output. Output its elements one at a time";
	case TypeKind::Record:  return "A whole record cannot be output. Output its fields one at a time";
	case TypeKind::Class:   return "An object cannot be output. Output its attributes or the results of its methods";
	case TypeKind::Pointer: return "A pointer cannot be output. Put ^ after it to use the value it points to";
	case TypeKind::Set:     return "A set cannot be output";
	default:                return {};
	}
}

bool isOutputtable(const Type* type) {
	return type->isAtomic() || type->kind == TypeKind::Enum;
}

}

Analyzer::Analyzer(const Program& program, TypeTable& types, const Environment& environment, Options options)
	: program_(program), types_(types), environment_(environment), options_(options) {}

void Analyzer::error(const char* code, SourceLocation location, std::string message) {
	Diagnostic diagnostic;
	diagnostic.severity = Severity::Error;
	diagnostic.phase = Phase::Semantic;
	diagnostic.code = code;
	diagnostic.line = location.line;
	diagnostic.column = location.column;
	diagnostic.message = std::move(message);
	diagnostics_.push_back(std::move(diagnostic));
}

void Analyzer::warning(const char* code, SourceLocation location, std::string message) {
	error(code, location, std::move(message));
	diagnostics_.back().severity = Severity::Warning;
}

void Analyzer::analyze() {
	// Global variables first: subroutines are hoisted (spec 3), so their
	// bodies may use globals declared anywhere in the main program.
	collectGlobals(program_.statements);
	block(program_.statements);

	for (const TypeDef& def : program_.types) {
		std::visit(Overloaded{
			[&](const RecordTypeDef& record) {
				for (const DeclareStmt& field : record.fields) noteTypeSpelling(field.type);
			},
			[&](const PointerTypeDef& pointer) { noteTypeSpelling(pointer.target); },
			[&](const SetTypeDef& set) { noteTypeSpelling(set.element); },
			[](const EnumTypeDef&) {},
		}, def.body);
	}
	for (const Subroutine& subroutine : program_.subroutines) {
		analyzeSubroutine(subroutine, nullptr);
	}
	for (const ClassDef& classDef : program_.classes) {
		const Type* classType = types_.find(classDef.name.name);
		if (!classType || classType->classDef != &classDef) {
			continue; // a duplicate, already reported
		}
		if (classDef.parent) {
			if (const Type* parent = types_.find(classDef.parent->name)) {
				noteSpelling(classDef.parent->name, parent->name, classDef.parent->location, parent->location);
			}
		}
		for (const Attribute& attribute : classDef.attributes) {
			noteTypeSpelling(attribute.type);
		}
		// Initialisers run on the new object, so attributes are visible.
		Scope scope;
		context_ = { nullptr, classType, &scope };
		block(classDef.initialisers);
		context_ = {};
		for (const Subroutine& method : classDef.methods) {
			analyzeSubroutine(method, classType);
		}
	}

	reportUnused(globals_);
}

// ---------- declarations ----------

// Spec 5.1: the main program's variables, from DECLARE, CONSTANT, DEFINE and
// (outside strict mode) the first assignment to an undeclared name, in the
// order they appear. There is no block scope, so loop and IF bodies count.
void Analyzer::collectGlobals(const Block& statements) {
	for (const StmtPtr& stmt : statements) {
		declareGlobal(*stmt);
		std::visit(Overloaded{
			[&](const IfStmt& node) { collectGlobals(node.thenBranch); collectGlobals(node.elseBranch); },
			[&](const CaseStmt& node) {
				for (const CaseClause& clause : node.clauses) collectGlobals(clause.body);
				if (node.otherwise) collectGlobals(*node.otherwise);
			},
			[&](const ForStmt& node) { collectGlobals(node.body); },
			[&](const RepeatStmt& node) { collectGlobals(node.body); },
			[&](const WhileStmt& node) { collectGlobals(node.body); },
			[](const auto&) {},
		}, stmt->node);
	}
}

void Analyzer::declareGlobal(const Stmt& stmt) {
	auto implicit = [&](const Expr* target, const Type* type) {
		const auto* name = target ? std::get_if<NameExpr>(&target->node) : nullptr;
		if (!name || options_.strict || globals_.symbols.count(toUpper(name->name))) {
			return;
		}
		std::string key = toUpper(name->name);
		if (environment_.findSubroutine(key) || types_.find(key) || types_.findEnumValue(key) || isBuiltinFunction(key)) {
			return; // reported where it is used
		}
		addSymbol(globals_, { Symbol::Kind::Implicit, name->name, type, target->location }, false);
	};

	std::visit(Overloaded{
		[&](const DeclareStmt& node) {
			const Type* type = types_.resolve(node.type);
			if (checkNameIsFree(node.name)) {
				addSymbol(globals_, { Symbol::Kind::Variable, node.name.name, type, node.name.location }, true);
			}
		},
		[&](const ConstantStmt& node) {
			if (checkNameIsFree(node.name)) {
				Value value = literalValue(node.value);
				addSymbol(globals_, { Symbol::Kind::Constant, node.name.name, typeOf(value), node.name.location, value }, true);
			}
		},
		[&](const DefineStmt& node) {
			const Type* setType = types_.find(node.setType.name);
			if (checkNameIsFree(node.name)) {
				addSymbol(globals_, { Symbol::Kind::Constant, node.name.name,
					setType && setType->kind == TypeKind::Set ? setType : nullptr, node.name.location }, true);
			}
		},
		[&](const AssignStmt& node) { implicit(node.target.get(), nullptr); },
		[&](const InputStmt& node) { implicit(node.target.get(), nullptr); },
		[&](const ReadFileStmt& node) { implicit(node.target.get(), stringType()); },
		[&](const GetRecordStmt& node) { implicit(node.target.get(), nullptr); },
		[&](const ForStmt& node) {
			NameExpr name{ node.variable.name };
			Expr target{ node.variable.location, name };
			implicit(&target, integerType());
		},
		[](const auto&) {},
	}, stmt.node);
}

// Spec 5.8: variables share one namespace with types, subroutines and
// classes, and built-in names are reserved.
bool Analyzer::checkNameIsFree(const Identifier& name) {
	if (const Subroutine* subroutine = environment_.findSubroutine(name.name)) {
		error("M002", name.location, name.name + " is already the name of a " + subroutineKind(*subroutine)
			+ " on line " + std::to_string(subroutine->location.line));
		return false;
	}
	if (const Type* type = types_.find(name.name)) {
		error("M002", name.location, name.name + " is already the name of a "
			+ std::string(type->kind == TypeKind::Class ? "class" : "type") + " on line " + std::to_string(type->location.line));
		return false;
	}
	if (auto constant = types_.findEnumValue(name.name)) {
		error("M002", name.location, name.name + " is already a value of the enumerated type " + constant->type->name);
		return false;
	}
	if (isBuiltinFunction(name.name)) {
		error("M002", name.location, name.name + " is a built-in function, so it cannot be used as a name");
		return false;
	}
	return true;
}

Analyzer::Symbol* Analyzer::addSymbol(Scope& scope, Symbol symbol, bool reportDuplicate) {
	std::string key = toUpper(symbol.name);
	if (auto existing = scope.symbols.find(key); existing != scope.symbols.end()) {
		if (reportDuplicate) {
			const Symbol& old = existing->second;
			std::string line = std::to_string(old.location.line);
			switch (old.kind) {
			case Symbol::Kind::Implicit:
				error("M002", symbol.location, old.name + " is already in use (from line " + line
					+ "), so it cannot be declared here. Put the DECLARE before its first use");
				break;
			case Symbol::Kind::Parameter:
				error("M002", symbol.location, old.name + " is already a parameter of "
					+ (context_.subroutine ? context_.subroutine->name.name : std::string("this subroutine")));
				break;
			case Symbol::Kind::Constant:
				error("M002", symbol.location, old.name + " is already a constant, defined on line " + line);
				break;
			case Symbol::Kind::Variable:
				error("M002", symbol.location, old.name + " is already declared on line " + line);
				break;
			}
		}
		return nullptr;
	}
	return &scope.symbols.emplace(std::move(key), std::move(symbol)).first->second;
}

void Analyzer::analyzeSubroutine(const Subroutine& subroutine, const Type* methodClass) {
	Scope locals;
	context_ = { &subroutine, methodClass, &locals };
	const Signature& signature = environment_.signature(subroutine);
	for (size_t i = 0; i < subroutine.parameters.size(); ++i) {
		const Parameter& parameter = subroutine.parameters[i];
		noteTypeSpelling(parameter.type);
		addSymbol(locals, { Symbol::Kind::Parameter, parameter.name.name, signature.parameterTypes[i],
			parameter.name.location }, false);
	}
	if (subroutine.returnType) {
		noteTypeSpelling(*subroutine.returnType);
	}
	block(subroutine.body);
	reportUnused(locals);
	context_ = {};
}

void Analyzer::reportUnused(const Scope& scope) {
	for (const auto& [key, symbol] : scope.symbols) {
		if (symbol.kind == Symbol::Kind::Variable && !symbol.used) {
			warning("W003", symbol.location, symbol.name + " is declared but never used");
		}
	}
}

// ---------- names ----------

// Spec 5.8: the local scope, then the current object's visible attributes
// and methods, then the global scope and the program's definitions, then
// the built-ins. Mirrors Environment::lookup.
Analyzer::Resolved Analyzer::resolve(const std::string& written, SourceLocation location) {
	std::string key = toUpper(written);
	Resolved result;

	if (context_.locals) {
		if (auto found = context_.locals->symbols.find(key); found != context_.locals->symbols.end()) {
			noteSpelling(written, found->second.name, location, found->second.location);
			result.kind = Resolved::Kind::Symbol;
			result.symbol = &found->second;
			result.type = found->second.type;
			result.name = found->second.name;
			return result;
		}
	}

	if (const Type* classType = context_.methodClass) {
		int index = classType->findField(written);
		if (index >= 0) {
			const Field& field = classType->fields[static_cast<size_t>(index)];
			if (field.visibility == Visibility::Public || field.owner == classType) {
				noteSpelling(written, field.name, location, field.location);
				result.kind = Resolved::Kind::Attribute;
				result.field = &field;
				result.type = field.type;
				result.name = field.name;
				return result;
			}
		}
		const Method* method = classType->findMethod(written);
		if (method && (method->definition->visibility == Visibility::Public || method->owner == classType)) {
			noteSpelling(written, method->name, location, method->definition->name.location);
			result.kind = Resolved::Kind::Method;
			result.method = method;
			result.subroutine = method->definition;
			result.name = method->name;
			return result;
		}
	}

	if (auto found = globals_.symbols.find(key); found != globals_.symbols.end()) {
		Symbol& symbol = found->second;
		noteSpelling(written, symbol.name, location, symbol.location);
		// In strict mode a variable must be declared before it is used (spec 5.1).
		bool beforeDeclaration = !context_.locals && symbol.kind != Symbol::Kind::Implicit
			&& (location.line < symbol.location.line
				|| (location.line == symbol.location.line && location.column < symbol.location.column));
		if (options_.strict && beforeDeclaration) {
			error("M001", location, symbol.name + " is used before its declaration on line "
				+ std::to_string(symbol.location.line));
		}
		result.kind = Resolved::Kind::Symbol;
		result.symbol = &symbol;
		result.type = symbol.type;
		result.name = symbol.name;
		return result;
	}

	if (auto constant = types_.findEnumValue(written)) {
		const std::string& valueName = constant->type->enumValues[static_cast<size_t>(constant->ordinal)];
		noteSpelling(written, valueName, location, constant->type->location);
		result.kind = Resolved::Kind::EnumValue;
		result.type = constant->type;
		result.name = valueName;
		result.enumValue = EnumValue{ constant->type, constant->ordinal };
		return result;
	}
	if (const Subroutine* subroutine = environment_.findSubroutine(written)) {
		noteSpelling(written, subroutine->name.name, location, subroutine->name.location);
		result.kind = Resolved::Kind::Subroutine;
		result.subroutine = subroutine;
		result.name = subroutine->name.name;
		return result;
	}
	if (const Type* type = types_.find(written)) {
		noteSpelling(written, type->name, location, type->location);
		result.kind = Resolved::Kind::Type;
		result.type = type;
		result.name = type->name;
		return result;
	}
	if (isBuiltinFunction(written)) {
		noteSpelling(written, key, location, {});
		result.kind = Resolved::Kind::Builtin;
		result.name = key;
		return result;
	}
	return result;
}

// W001: identifiers ignore case (spec 2.2), but writing one name two ways
// is usually a slip. Reported once per spelling.
void Analyzer::noteSpelling(std::string_view written, std::string_view canonical, SourceLocation location,
	SourceLocation definedAt) {
	if (written == canonical) {
		return;
	}
	std::string key = toUpper(canonical) + '\n' + std::string(written);
	if (!spellingsWarned_.insert(key).second) {
		return;
	}
	std::string where = definedAt.line > 0 ? " (line " + std::to_string(definedAt.line) + ")" : "";
	warning("W001", location, std::string(written) + " and " + std::string(canonical) + where
		+ " are the same name written with different capitalisation");
}

void Analyzer::noteTypeSpelling(const TypeSpec& spec) {
	std::visit(Overloaded{
		[&](const NamedType& named) {
			if (const Type* type = types_.find(named.name)) {
				noteSpelling(named.name, type->name, spec.location, type->location);
			}
		},
		[&](const ArrayType& array) {
			if (array.element) noteTypeSpelling(*array.element);
		},
		[](BuiltinType) {},
	}, spec.node);
}

// Inside a subclass's method, a parent's PRIVATE member is hidden (spec 11.2,
// D15), so its bare name finds nothing. Says why, rather than leaving it to
// look like an undeclared variable.
bool Analyzer::reportHiddenPrivate(const std::string& written, SourceLocation location) {
	const Type* classType = context_.methodClass;
	if (!classType) {
		return false;
	}
	std::string owner;
	int index = classType->findField(written);
	if (index >= 0) {
		owner = classType->fields[static_cast<size_t>(index)].owner->name;
	}
	else if (const Method* method = classType->findMethod(written)) {
		owner = method->owner->name;
	}
	else {
		return false;
	}
	error("M009", location, written + " is PRIVATE in " + owner + ", so " + classType->name
		+ "'s methods cannot use it. Use a public method of " + owner + " instead");
	return true;
}

bool Analyzer::isLoopVariable(std::string_view name) const {
	return std::find(loopVariables_.begin(), loopVariables_.end(), toUpper(name)) != loopVariables_.end();
}

// ---------- statements ----------

void Analyzer::block(const Block& statements) {
	for (const StmtPtr& stmt : statements) {
		statement(*stmt);
	}
}

void Analyzer::statement(const Stmt& stmt) {
	std::visit(Overloaded{
		[&](const DeclareStmt& node) { declare(node, stmt); },
		[&](const ConstantStmt& node) {
			if (context_.locals && checkNameIsFree(node.name)) {
				Value value = literalValue(node.value);
				addSymbol(*context_.locals, { Symbol::Kind::Constant, node.name.name, typeOf(value), node.name.location, value }, true);
			}
		},
		[&](const AssignStmt& node) { assign(node); },
		[&](const InputStmt& node) {
			const Type* type = target(*node.target);
			if (type && !type->isAtomic()) {
				error("M004", node.target->location, "INPUT cannot read " + describeType(type)
					+ ". It reads INTEGER, REAL, CHAR, STRING, BOOLEAN or DATE values");
			}
		},
		[&](const OutputStmt& node) {
			for (const ExprPtr& value : node.values) {
				const Type* type = expression(*value);
				if (type && !isOutputtable(type)) {
					error("M004", value->location, notOutputtable(type));
				}
			}
		},
		[&](const CallStmt& node) { callStatement(node, stmt); },
		[&](const IfStmt& node) {
			requireType(*node.condition, TypeKind::Boolean, "The IF condition");
			block(node.thenBranch);
			block(node.elseBranch);
		},
		[&](const CaseStmt& node) { caseStatement(node); },
		[&](const ForStmt& node) { forStatement(node); },
		[&](const RepeatStmt& node) {
			block(node.body);
			requireType(*node.condition, TypeKind::Boolean, "The UNTIL condition");
		},
		[&](const WhileStmt& node) {
			requireType(*node.condition, TypeKind::Boolean, "The WHILE condition");
			block(node.body);
		},
		[&](const ReturnStmt& node) { returnStatement(node, stmt); },
		[&](const DefineStmt& node) { define(node, stmt); },
		[&](const auto&) { fileStatement(stmt); },
	}, stmt.node);
}

void Analyzer::declare(const DeclareStmt& node, const Stmt&) {
	noteTypeSpelling(node.type);
	// Spec 5.3: bounds are INTEGER expressions.
	auto checkBounds = [&](auto& self, const TypeSpec& spec) -> void {
		if (const auto* array = std::get_if<ArrayType>(&spec.node)) {
			for (const ArrayBounds& bounds : array->bounds) {
				requireType(*bounds.lower, TypeKind::Integer, "An array bound");
				requireType(*bounds.upper, TypeKind::Integer, "An array bound");
			}
			if (array->element) self(self, *array->element);
		}
	};
	checkBounds(checkBounds, node.type);

	if (!context_.locals) {
		return; // the main program's variables were collected first
	}
	const Type* type = types_.resolve(node.type);
	if (checkNameIsFree(node.name)) {
		addSymbol(*context_.locals, { Symbol::Kind::Variable, node.name.name, type, node.name.location }, true);
	}
}

// Spec 7.1.
void Analyzer::assign(const AssignStmt& node) {
	const Type* valueType = expression(*node.value);
	const Type* targetType = target(*node.target);
	if (targetType && valueType && !isAssignable(targetType, valueType)) {
		error("M004", node.value->location, "Cannot assign to " + nameOf(*node.target) + ". "
			+ assignmentError(targetType, valueType));
	}
}

void Analyzer::callStatement(const CallStmt& node, const Stmt& stmt) {
	call(*node.callee, node.arguments, stmt.location, true, node.usesCallKeyword);
}

// Spec 7.4.
void Analyzer::caseStatement(const CaseStmt& node) {
	const Type* selector = expression(*node.selector);
	if (selector && selector->kind != TypeKind::Integer && selector->kind != TypeKind::Char
		&& selector->kind != TypeKind::String && selector->kind != TypeKind::Enum && selector->kind != TypeKind::Date) {
		error("M004", node.selector->location, "A CASE can only choose on an INTEGER, CHAR, STRING, DATE or enumerated value, but this is "
			+ describeType(selector));
	}

	struct KnownLabel {
		Value low;
		Value high;
		std::string text;
		int line;
	};
	std::vector<KnownLabel> known;

	for (const CaseClause& clause : node.clauses) {
		for (const CaseLabel& label : clause.labels) {
			for (const Expr* part : { label.value.get(), label.upTo.get() }) {
				if (!part) continue;
				const Type* type = expression(*part);
				OperatorCheck check = checkBinary(label.upTo ? BinaryOp::LessEqual : BinaryOp::Equal, selector, type);
				if (!check.ok()) {
					error("M004", part->location, "This label can never match: " + check.error);
				}
			}

			// W002: a label overlapping an earlier one (spec 7.4).
			std::optional<Value> low = constantValue(*label.value);
			std::optional<Value> high = label.upTo ? constantValue(*label.upTo) : low;
			if (!low || !high) {
				continue;
			}
			std::string text = labelText(*low) + (label.upTo ? " TO " + labelText(*high) : "");
			for (const KnownLabel& earlier : known) {
				try {
					bool overlaps = compareValues(*low, earlier.high) <= 0 && compareValues(earlier.low, *high) <= 0;
					if (!overlaps) continue;
					bool covered = compareValues(earlier.low, *low) <= 0 && compareValues(*high, earlier.high) <= 0;
					warning("W002", label.value->location, "The label " + text + " overlaps " + earlier.text + " on line "
						+ std::to_string(earlier.line) + ", which is tested first, so "
						+ (covered ? "this label can never match" : "the values they share go to the earlier label"));
					break;
				}
				catch (const RuntimeError&) {
					// Labels of other types: already reported above.
				}
			}
			known.push_back({ *low, *high, text, label.value->location.line });
		}
		block(clause.body);
	}
	if (node.otherwise) {
		block(*node.otherwise);
	}
}

// Spec 7.5.
void Analyzer::forStatement(const ForStmt& node) {
	NameExpr variableName{ node.variable.name };
	Expr variable{ node.variable.location, variableName };
	const Type* type = target(variable, integerType());
	if (type && type->kind != TypeKind::Integer) {
		error("M004", node.variable.location, "The FOR loop variable " + node.variable.name + " must be an INTEGER, but it is "
			+ describeType(type));
	}
	requireType(*node.start, TypeKind::Integer, "The FOR start value");
	requireType(*node.end, TypeKind::Integer, "The FOR end value");
	if (node.step) {
		requireType(*node.step, TypeKind::Integer, "The STEP value");
	}
	loopVariables_.push_back(toUpper(node.variable.name));
	block(node.body);
	loopVariables_.pop_back();
}

// Spec 8.2.
void Analyzer::returnStatement(const ReturnStmt& node, const Stmt& stmt) {
	const Subroutine* function = context_.subroutine;
	const Type* valueType = expression(*node.value);
	if (!function) {
		error("M008", stmt.location, "RETURN can only be used inside a FUNCTION");
		return;
	}
	if (!function->isFunction) {
		error("M008", stmt.location, "A PROCEDURE does not use RETURN: it ends at ENDPROCEDURE");
		return;
	}
	const Type* returnType = environment_.signature(*function).returnType;
	if (returnType && valueType && !isAssignable(returnType, valueType)) {
		error("M004", node.value->location, "The function " + function->name.name + " must return "
			+ describeType(returnType) + ". " + assignmentError(returnType, valueType));
	}
}

// Spec 10.
void Analyzer::fileStatement(const Stmt& stmt) {
	std::visit(Overloaded{
		[&](const OpenFileStmt& node) { requireFileName(*node.file); },
		[&](const CloseFileStmt& node) { requireFileName(*node.file); },
		[&](const ReadFileStmt& node) {
			requireFileName(*node.file);
			const Type* type = target(*node.target, stringType());
			if (type && type->kind != TypeKind::String) {
				error("M004", node.target->location, "READFILE reads a line of text, so " + nameOf(*node.target)
					+ " must be a STRING, but it is " + describeType(type) + ". Read into a STRING, then convert it with STR_TO_NUM");
			}
		},
		[&](const WriteFileStmt& node) {
			requireFileName(*node.file);
			const Type* type = expression(*node.value);
			if (type && !isOutputtable(type)) {
				error("M004", node.value->location, "WRITEFILE writes one value per line, so it cannot write " + describeType(type));
			}
		},
		[&](const SeekStmt& node) {
			requireFileName(*node.file);
			requireType(*node.address, TypeKind::Integer, "The record address");
		},
		[&](const GetRecordStmt& node) {
			requireFileName(*node.file);
			const Type* type = target(*node.target);
			if (type && (type->kind == TypeKind::Class || type->kind == TypeKind::Pointer)) {
				error("M004", node.target->location, "GETRECORD cannot read " + describeType(type) + " from a random file");
			}
		},
		[&](const PutRecordStmt& node) {
			requireFileName(*node.file);
			const Type* type = expression(*node.source);
			if (type && (type->kind == TypeKind::Class || type->kind == TypeKind::Pointer)) {
				error("M004", node.source->location, type->kind == TypeKind::Class
					? "An object cannot be stored in a random file. Store its attributes in a record instead"
					: "A pointer cannot be stored in a file: the address it holds would mean nothing when read back");
			}
		},
		[](const auto&) {},
	}, stmt.node);
}

// Spec 5.7.
void Analyzer::define(const DefineStmt& node, const Stmt&) {
	const Type* setType = types_.find(node.setType.name);
	if (!setType) {
		error("M001", node.setType.location, "Unknown type " + node.setType.name);
	}
	else {
		noteSpelling(node.setType.name, setType->name, node.setType.location, setType->location);
		if (setType->kind != TypeKind::Set) {
			error("M004", node.setType.location, node.setType.name + " is not a SET type, so DEFINE cannot use it");
		}
		else {
			for (const Literal& literal : node.values) {
				Value value = literalValue(literal);
				if (!isAssignable(setType->element, typeOf(value))) {
					error("M004", node.name.location, "The set " + node.name.name + " holds " + typeName(setType->element)
						+ " values, but " + labelText(value) + " is " + describeType(typeOf(value)));
				}
			}
		}
	}
	if (context_.locals && checkNameIsFree(node.name)) {
		addSymbol(*context_.locals, { Symbol::Kind::Constant, node.name.name, setType, node.name.location }, true);
	}
}

void Analyzer::requireType(const Expr& expr, TypeKind kind, std::string_view what) {
	const Type* type = expression(expr);
	if (!type || type->kind == kind) {
		return;
	}
	std::string message = std::string(what) + " must be "
		+ (kind == TypeKind::Boolean ? "TRUE or FALSE" : "an INTEGER") + ", but this is " + describeType(type);
	if (kind == TypeKind::Integer && type->kind == TypeKind::Real) {
		message += ". Use INT() to convert it";
	}
	error("M004", expr.location, message);
}

void Analyzer::requireFileName(const Expr& expr) {
	const Type* type = expression(expr);
	if (type && !type->isText()) {
		error("M004", expr.location, "A file name must be a STRING, such as \"Data.txt\", but this is " + describeType(type));
	}
}

// ---------- expressions ----------

const Type* Analyzer::expression(const Expr& expr) {
	return std::visit(Overloaded{
		[&](const LiteralExpr& node) { return typeOf(literalValue(node.value)); },
		[&](const NameExpr& node) { return name(node, expr.location); },
		[&](const UnaryExpr& node) -> const Type* {
			OperatorCheck check = checkUnary(node.op, expression(*node.operand));
			if (!check.ok()) {
				error("M004", expr.location, check.error);
			}
			return check.result;
		},
		[&](const BinaryExpr& node) -> const Type* {
			const Type* left = expression(*node.left);
			const Type* right = expression(*node.right);
			OperatorCheck check = checkBinary(node.op, left, right);
			if (!check.ok()) {
				error("M004", expr.location, check.error);
			}
			return check.result;
		},
		[&](const IndexExpr& node) { return index(node, expr.location); },
		[&](const MemberExpr& node) { return member(node); },
		[&](const DerefExpr& node) -> const Type* {
			const Type* type = expression(*node.pointer);
			if (type && type->kind != TypeKind::Pointer) {
				error("M004", expr.location, nameOf(*node.pointer) + " is not a pointer, so ^ cannot follow it");
				return nullptr;
			}
			return type ? type->element : nullptr;
		},
		[&](const CallExpr& node) { return call(*node.callee, node.arguments, expr.location, false, true); },
		[&](const AddressOfExpr& node) -> const Type* {
			if (const auto* name = std::get_if<NameExpr>(&node.target->node)) {
				Resolved resolved = resolve(name->name, node.target->location);
				if (resolved.symbol && resolved.symbol->kind == Symbol::Kind::Constant) {
					error("M004", expr.location, resolved.name + " is a constant, so a pointer to it cannot be made");
					return nullptr;
				}
			}
			const Type* type = expression(*node.target);
			return type ? types_.pointerTo(type) : nullptr;
		},
		[&](const NewExpr& node) { return construct(node, expr.location); },
		[&](const SuperExpr& node) -> const Type* {
			error("M005", expr.location, "SUPER." + node.member.name + " must be called, as in SUPER." + node.member.name + "(...)");
			return nullptr;
		},
	}, expr.node);
}

// A name used for its value (spec 6.3).
const Type* Analyzer::name(const NameExpr& node, SourceLocation location) {
	Resolved resolved = resolve(node.name, location);
	switch (resolved.kind) {
	case Resolved::Kind::Symbol:
		resolved.symbol->used = true;
		return resolved.type;
	case Resolved::Kind::Attribute:
	case Resolved::Kind::EnumValue:
		return resolved.type;
	case Resolved::Kind::Subroutine:
	case Resolved::Kind::Method:
		if (!resolved.subroutine->isFunction) {
			error("M005", location, resolved.name + " is a procedure and does not return a value; use CALL");
		}
		else {
			error("M005", location, resolved.name + " is a function, so it needs brackets to be called, as in "
				+ resolved.name + "(...)");
		}
		return nullptr;
	case Resolved::Kind::Type:
		error("M004", location, resolved.name + " is a type, not a value");
		return nullptr;
	case Resolved::Kind::Builtin:
		error("M005", location, resolved.name + " is a built-in function, so it needs brackets to be called, as in "
			+ resolved.name + "(...)");
		return nullptr;
	case Resolved::Kind::None:
		if (!reportHiddenPrivate(node.name, location) && options_.strict) {
			error("M001", location, node.name + " has not been declared");
		}
		return nullptr;
	}
	return nullptr;
}

// Something being given a value: by assignment, INPUT, READFILE, GETRECORD,
// FOR or a BYREF argument. An undeclared name becomes a variable here
// (spec 5.1), typed `typeIfNew` if that is known.
const Type* Analyzer::target(const Expr& expr, const Type* typeIfNew) {
	const auto* nameNode = std::get_if<NameExpr>(&expr.node);
	if (!nameNode) {
		return expression(expr);
	}
	const std::string& written = nameNode->name;
	Resolved resolved = resolve(written, expr.location);
	switch (resolved.kind) {
	case Resolved::Kind::Symbol:
		resolved.symbol->used = true;
		if (resolved.symbol->kind == Symbol::Kind::Constant) {
			error("M007", expr.location, resolved.name + " is a constant, so its value cannot be changed");
		}
		else if (isLoopVariable(written)) {
			error("M007", expr.location, resolved.name + " is the variable of a FOR loop, so it cannot be changed inside that loop");
		}
		return resolved.type;
	case Resolved::Kind::Attribute:
		if (isLoopVariable(written)) {
			error("M007", expr.location, resolved.name + " is the variable of a FOR loop, so it cannot be changed inside that loop");
		}
		return resolved.type;
	case Resolved::Kind::EnumValue:
		error("M007", expr.location, resolved.name + " is a value of the enumerated type " + resolved.type->name
			+ ", so it cannot be changed");
		return nullptr;
	case Resolved::Kind::Subroutine:
	case Resolved::Kind::Method:
		error("M002", expr.location, resolved.name + " is a " + subroutineKind(*resolved.subroutine)
			+ ", so it cannot be used as a variable"
			+ (resolved.subroutine->isFunction ? ". A function gives its result with RETURN" : ""));
		return nullptr;
	case Resolved::Kind::Type:
		error("M002", expr.location, resolved.name + " is a type, so it cannot be used as a variable");
		return nullptr;
	case Resolved::Kind::Builtin:
		error("M002", expr.location, resolved.name + " is a built-in function, so it cannot be used as a variable");
		return nullptr;
	case Resolved::Kind::None:
		break;
	}

	if (reportHiddenPrivate(written, expr.location)) {
		return nullptr;
	}
	if (options_.strict) {
		error("M001", expr.location, written + " has not been declared. In strict mode every variable must be declared with DECLARE");
		return nullptr;
	}
	Scope& scope = context_.locals ? *context_.locals : globals_;
	addSymbol(scope, { Symbol::Kind::Implicit, written, typeIfNew, expr.location }, false);
	return typeIfNew;
}

// Spec 5.3.
const Type* Analyzer::index(const IndexExpr& node, SourceLocation location) {
	const Type* array = expression(*node.array);
	for (const ExprPtr& indexExpr : node.indices) {
		requireType(*indexExpr, TypeKind::Integer, "An array index");
	}
	if (!array) {
		return nullptr;
	}
	if (array->kind != TypeKind::Array) {
		error("M004", location, nameOf(*node.array) + " is " + describeType(array) + ", not an array, so it cannot be indexed with [ ]");
		return nullptr;
	}
	size_t given = node.indices.size();
	if (array->dimensions > 0 && given != static_cast<size_t>(array->dimensions)) {
		error("M004", location, nameOf(*node.array) + " has " + std::to_string(array->dimensions)
			+ (array->dimensions == 1 ? " dimension" : " dimensions") + ", so it needs " + std::to_string(array->dimensions)
			+ (array->dimensions == 1 ? " index" : " indices") + ", but " + std::to_string(given)
			+ (given == 1 ? " was" : " were") + " given");
	}
	return array->element;
}

// Record.Field and Object.Attribute (spec 5.6, 11.2).
const Type* Analyzer::member(const MemberExpr& node) {
	const Type* object = expression(*node.object);
	const std::string& written = node.member.name;
	SourceLocation location = node.member.location;
	if (!object) {
		return nullptr;
	}
	switch (object->kind) {
	case TypeKind::Record: {
		int found = object->findField(written);
		if (found < 0) {
			error("M001", location, "The record type " + object->name + " has no field called " + written);
			return nullptr;
		}
		const Field& field = object->fields[static_cast<size_t>(found)];
		noteSpelling(written, field.name, location, field.location);
		return field.type;
	}
	case TypeKind::Class: {
		int found = object->findField(written);
		if (found < 0) {
			if (object->findMethod(written)) {
				error("M005", location, written + " is a method of " + object->name + ", so it needs brackets to be called");
			}
			else {
				error("M001", location, object->name + " has no attribute called " + written);
			}
			return nullptr;
		}
		const Field& field = object->fields[static_cast<size_t>(found)];
		noteSpelling(written, field.name, location, field.location);
		if (field.visibility == Visibility::Private && field.owner != context_.methodClass) {
			error("M009", location, field.name + " is PRIVATE in " + field.owner->name
				+ ", so it can only be used inside " + field.owner->name + "'s own methods");
		}
		return field.type;
	}
	case TypeKind::Pointer:
		error("M004", location, nameOf(*node.object) + " is a pointer. Put ^ after it to reach what it points to, as in "
			+ nameOf(*node.object) + "^." + written);
		return nullptr;
	default:
		error("M004", location, nameOf(*node.object) + " is " + describeType(object) + ", so it has no fields");
		return nullptr;
	}
}

// A call as an expression or a statement (spec 8.1, 8.2, 9, 11.4).
const Type* Analyzer::call(const Expr& callee, const std::vector<ExprPtr>& arguments, SourceLocation location,
	bool asStatement, bool usesCallKeyword) {
	auto evaluateArguments = [&] {
		for (const ExprPtr& argument : arguments) {
			expression(*argument);
		}
	};
	const Subroutine* subroutine = nullptr;
	std::string what;

	if (const auto* name = std::get_if<NameExpr>(&callee.node)) {
		Resolved resolved = resolve(name->name, callee.location);
		switch (resolved.kind) {
		case Resolved::Kind::Subroutine:
			subroutine = resolved.subroutine;
			if (asStatement && !usesCallKeyword && !subroutine->isFunction) {
				error("M005", location, "To call the procedure " + resolved.name + ", write CALL " + resolved.name + "(...)");
			}
			break;
		case Resolved::Kind::Method:
			subroutine = resolved.subroutine;
			break;
		case Resolved::Kind::Builtin: {
			if (asStatement) {
				error("M005", location, resolved.name + " is a built-in function, so it cannot be used as a statement. "
					"Use its result in an expression");
			}
			std::vector<const Type*> types;
			for (const ExprPtr& argument : arguments) {
				types.push_back(expression(*argument));
			}
			BuiltinCheck check = checkBuiltinCall(resolved.name, types);
			if (!check.ok()) {
				SourceLocation at = check.code == "M004" ? arguments[check.argument]->location : location;
				error(check.code == "M004" ? "M004" : "M003", at, check.error);
			}
			return check.result;
		}
		case Resolved::Kind::Symbol:
		case Resolved::Kind::Attribute:
			if (resolved.symbol) resolved.symbol->used = true;
			if (resolved.type && resolved.type->kind == TypeKind::Array) {
				error("M005", location, resolved.name + " is an array. Use square brackets for an index, as in "
					+ resolved.name + "[1]");
			}
			else {
				error("M005", location, resolved.name + " is not a procedure or function, so it cannot be called");
			}
			evaluateArguments();
			return nullptr;
		case Resolved::Kind::EnumValue:
		case Resolved::Kind::Type:
			error("M005", location, resolved.name + " is not a procedure or function, so it cannot be called");
			evaluateArguments();
			return nullptr;
		case Resolved::Kind::None:
			if (!reportHiddenPrivate(name->name, callee.location)) {
				error("M001", callee.location, "There is no procedure or function called " + name->name);
			}
			evaluateArguments();
			return nullptr;
		}
		what = subroutineKind(*subroutine) + " " + resolved.name;
	}
	else if (const auto* memberNode = std::get_if<MemberExpr>(&callee.node)) {
		const Type* object = expression(*memberNode->object);
		const std::string& written = memberNode->member.name;
		SourceLocation at = memberNode->member.location;
		if (object && object->kind == TypeKind::Class) {
			const Method* method = object->findMethod(written);
			if (!method) {
				if (object->findField(written) >= 0) {
					error("M005", at, written + " is an attribute of " + object->name + ", not a method");
				}
				else {
					error("M001", at, object->name + " has no method called " + written);
				}
			}
			else {
				noteSpelling(written, method->name, at, method->definition->name.location);
				if (method->definition->visibility == Visibility::Private && method->owner != context_.methodClass) {
					error("M009", at, method->name + " is PRIVATE in " + method->owner->name
						+ ", so it can only be called from " + method->owner->name + "'s own methods");
				}
				subroutine = method->definition;
				what = "method " + method->owner->name + "." + method->name;
			}
		}
		else if (object) {
			error("M004", at, nameOf(*memberNode->object) + " is " + describeType(object) + ", not an object, so it has no methods");
		}
		if (!subroutine) {
			evaluateArguments();
			return nullptr;
		}
	}
	else if (const auto* super = std::get_if<SuperExpr>(&callee.node)) {
		const Type* classType = context_.methodClass;
		if (!classType || !context_.subroutine) {
			error("M008", callee.location, "SUPER can only be used inside a method of a class");
			evaluateArguments();
			return nullptr;
		}
		if (!classType->parent) {
			error("M008", callee.location, classType->name + " does not inherit from another class, so it has no SUPER");
			evaluateArguments();
			return nullptr;
		}
		const Type* parent = classType->parent;
		if (toUpper(super->member.name) == "NEW") {
			const Method* constructor = Environment::constructorOf(parent);
			if (!constructor) {
				if (!arguments.empty()) {
					error("M003", location, parent->name + " has no constructor (PROCEDURE NEW), so SUPER.NEW takes no arguments");
				}
				evaluateArguments();
				return nullptr;
			}
			subroutine = constructor->definition;
			what = "constructor of " + parent->name;
		}
		else {
			const Method* method = parent->findMethod(super->member.name);
			if (!method) {
				error("M001", super->member.location, parent->name + " has no method called " + super->member.name);
				evaluateArguments();
				return nullptr;
			}
			if (method->definition->visibility == Visibility::Private && method->owner != classType) {
				error("M009", super->member.location, method->name + " is PRIVATE in " + method->owner->name
					+ ", so it can only be called from " + method->owner->name + "'s own methods");
			}
			subroutine = method->definition;
			what = "method " + method->owner->name + "." + method->name;
		}
	}
	else {
		error("M004", location, "This value cannot be called");
		evaluateArguments();
		return nullptr;
	}

	if (asStatement && subroutine->isFunction) {
		error("M005", location, subroutine->name.name + " is a function, so it cannot be used as a statement. "
			"Use its result in an expression, as in Result ← " + subroutine->name.name + "(...)");
	}
	if (!asStatement && !subroutine->isFunction) {
		error("M005", location, subroutine->name.name + " is a procedure and does not return a value; use CALL");
	}
	checkArguments(*subroutine, arguments, location, what);
	return subroutine->isFunction ? environment_.signature(*subroutine).returnType : nullptr;
}

// Spec 8.3.
void Analyzer::checkArguments(const Subroutine& subroutine, const std::vector<ExprPtr>& arguments, SourceLocation location,
	std::string_view what) {
	const std::vector<Parameter>& parameters = subroutine.parameters;
	const Signature& signature = environment_.signature(subroutine);
	if (arguments.size() != parameters.size()) {
		error("M003", location, "The " + std::string(what) + " needs " + std::to_string(parameters.size())
			+ (parameters.size() == 1 ? " argument" : " arguments") + ", but " + std::to_string(arguments.size())
			+ (arguments.size() == 1 ? " was" : " were") + " given");
		for (const ExprPtr& argument : arguments) {
			expression(*argument);
		}
		return;
	}

	for (size_t i = 0; i < arguments.size(); ++i) {
		const Parameter& parameter = parameters[i];
		const Expr& argument = *arguments[i];
		const Type* parameterType = signature.parameterTypes[i];
		std::string which = "parameter " + parameter.name.name + " of " + subroutine.name.name;

		if (parameter.mode == PassMode::ByValue) {
			const Type* argumentType = expression(argument);
			if (parameterType && argumentType && !isAssignable(parameterType, argumentType)) {
				error("M004", argument.location, "Wrong argument for " + which + ". " + assignmentError(parameterType, argumentType));
			}
			continue;
		}

		if (!isPlace(argument)) {
			error("M006", argument.location, "The BYREF " + which
				+ " needs a variable, array element or field to change, not an expression or literal");
			expression(argument);
			continue;
		}
		if (const auto* name = std::get_if<NameExpr>(&argument.node)) {
			Resolved resolved = resolve(name->name, argument.location);
			bool constant = (resolved.symbol && resolved.symbol->kind == Symbol::Kind::Constant)
				|| resolved.kind == Resolved::Kind::EnumValue;
			if (constant) {
				error("M006", argument.location, "A constant cannot be passed to the BYREF " + which);
				continue;
			}
		}
		const Type* argumentType = target(argument, parameterType);
		if (parameterType && argumentType && !sameType(parameterType, argumentType)
			&& !(parameterType->isOpenArray() && isAssignable(parameterType, argumentType))) {
			std::string message = "The BYREF " + which + " is " + typeName(parameterType)
				+ ", so the argument must be exactly that type, but it is " + typeName(argumentType);
			if (parameterType->kind == TypeKind::Real && argumentType->kind == TypeKind::Integer) {
				message += ". INTEGER does not become REAL for BYREF parameters";
			}
			error("M004", argument.location, message);
		}
	}
}

// Spec 11.3.
const Type* Analyzer::construct(const NewExpr& node, SourceLocation location) {
	const Type* classType = types_.find(node.className.name);
	if (!classType) {
		error("M001", node.className.location, "There is no class called " + node.className.name);
	}
	else {
		noteSpelling(node.className.name, classType->name, node.className.location, classType->location);
		if (classType->kind != TypeKind::Class) {
			error("M004", node.className.location, node.className.name + " is not a class, so NEW cannot create one");
			classType = nullptr;
		}
	}
	const Method* constructor = classType ? Environment::constructorOf(classType) : nullptr;
	if (constructor) {
		checkArguments(*constructor->definition, node.arguments, location, "constructor of " + classType->name);
		return classType;
	}
	if (classType && !node.arguments.empty()) {
		error("M003", location, classType->name + " has no constructor (PROCEDURE NEW), so NEW "
			+ classType->name + "() takes no arguments");
	}
	for (const ExprPtr& argument : node.arguments) {
		expression(*argument);
	}
	return classType;
}

// The value of a CASE label known before running: a literal, a constant or
// an enumerated value.
std::optional<Value> Analyzer::constantValue(const Expr& expr) {
	if (const auto* literal = std::get_if<LiteralExpr>(&expr.node)) {
		return literalValue(literal->value);
	}
	if (const auto* name = std::get_if<NameExpr>(&expr.node)) {
		Resolved resolved = resolve(name->name, expr.location);
		if (resolved.enumValue) {
			return Value(*resolved.enumValue);
		}
		if (resolved.symbol && resolved.symbol->kind == Symbol::Kind::Constant) {
			return resolved.symbol->value;
		}
	}
	return std::nullopt;
}
