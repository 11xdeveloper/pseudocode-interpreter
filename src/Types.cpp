#include "Types.h"

#include <algorithm>

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
	return a.size() == b.size() && toUpper(a) == toUpper(b);
}

Type makeBuiltin(TypeKind kind, std::string name) {
	Type type;
	type.kind = kind;
	type.name = std::move(name);
	return type;
}

bool isOrdering(BinaryOp op) {
	return op == BinaryOp::Less || op == BinaryOp::Greater
		|| op == BinaryOp::LessEqual || op == BinaryOp::GreaterEqual;
}

std::string capitalise(std::string text) {
	if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
		text[0] = static_cast<char>(text[0] - 'a' + 'A');
	}
	return text;
}

// Spec 4.4: why two values cannot be compared with `op`, or "" if they can.
std::string comparisonError(BinaryOp op, const Type* left, const Type* right) {
	auto kindError = [&](const Type* type) -> std::string {
		if (!type) return {};
		switch (type->kind) {
		case TypeKind::Array:  return "Arrays cannot be compared directly. Compare their elements one at a time";
		case TypeKind::Record: return "Records cannot be compared directly. Compare their fields one at a time";
		case TypeKind::Set:    return "Sets cannot be compared. Use IN to test whether a value is in a set";
		case TypeKind::Class:  return "Objects cannot be compared";
		case TypeKind::Boolean:
			if (isOrdering(op)) return "TRUE and FALSE have no order, so only = and <> work on BOOLEAN values";
			return {};
		case TypeKind::Pointer:
			if (isOrdering(op)) return "Pointers can only be compared with = and <>";
			return {};
		default:
			return {};
		}
	};
	for (const Type* side : { left, right }) {
		if (std::string error = kindError(side); !error.empty()) {
			return error;
		}
	}
	if (!left || !right) {
		return {};
	}
	if ((left->isNumeric() && right->isNumeric()) || (left->isText() && right->isText()) || sameType(left, right)) {
		return {};
	}
	std::string message = "Cannot compare " + describeType(left) + " with " + describeType(right);
	if ((left->isNumeric() && right->isText()) || (left->isText() && right->isNumeric())) {
		message += ". Use NUM_TO_STR or STR_TO_NUM to convert one of them";
	}
	return message;
}

}

// ---------- Type ----------

int Type::findField(std::string_view fieldName) const {
	for (size_t i = 0; i < fields.size(); ++i) {
		if (equalsIgnoreCase(fields[i].name, fieldName)) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

int Type::findEnumValue(std::string_view valueName) const {
	for (size_t i = 0; i < enumValues.size(); ++i) {
		if (equalsIgnoreCase(enumValues[i], valueName)) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

const Method* Type::findMethod(std::string_view methodName) const {
	for (const Type* type = this; type; type = type->parent) {
		for (const Method& method : type->methods) {
			if (equalsIgnoreCase(method.name, methodName)) {
				return &method;
			}
		}
	}
	return nullptr;
}

bool Type::isA(const Type* base) const {
	for (const Type* type = this; type; type = type->parent) {
		if (type == base) {
			return true;
		}
	}
	return false;
}

const Type* integerType() { static const Type type = makeBuiltin(TypeKind::Integer, "INTEGER"); return &type; }
const Type* realType()    { static const Type type = makeBuiltin(TypeKind::Real, "REAL"); return &type; }
const Type* charType()    { static const Type type = makeBuiltin(TypeKind::Char, "CHAR"); return &type; }
const Type* stringType()  { static const Type type = makeBuiltin(TypeKind::String, "STRING"); return &type; }
const Type* booleanType() { static const Type type = makeBuiltin(TypeKind::Boolean, "BOOLEAN"); return &type; }
const Type* dateType()    { static const Type type = makeBuiltin(TypeKind::Date, "DATE"); return &type; }

const Type* builtinType(BuiltinType type) {
	switch (type) {
	case BuiltinType::Integer: return integerType();
	case BuiltinType::Real:    return realType();
	case BuiltinType::Char:    return charType();
	case BuiltinType::String:  return stringType();
	case BuiltinType::Boolean: return booleanType();
	case BuiltinType::Date:    return dateType();
	}
	return nullptr;
}

std::string typeName(const Type* type) {
	return type ? type->name : "unknown";
}

std::string describeType(const Type* type) {
	if (!type) {
		return "a value of unknown type";
	}
	switch (type->kind) {
	case TypeKind::Integer: return "an " + type->name;
	case TypeKind::Array:   return (type->dimensions > 1 ? "a " : "an ") + type->name; // "a 2D ARRAY"
	case TypeKind::Record:  return "a " + type->name + " record";
	case TypeKind::Enum:    return "a " + type->name + " value";
	case TypeKind::Pointer: return "a pointer (" + type->name + ")";
	case TypeKind::Set:     return "a set (" + type->name + ")";
	case TypeKind::Class:   return "a " + type->name + " object";
	default:                return "a " + type->name;
	}
}

// ---------- TypeTable ----------

bool TypeTable::hasErrors() const {
	return std::any_of(diagnostics_.begin(), diagnostics_.end(),
		[](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::Error; });
}

void TypeTable::error(const char* code, SourceLocation location, std::string message) {
	Diagnostic diagnostic;
	diagnostic.severity = Severity::Error;
	diagnostic.phase = Phase::Semantic;
	diagnostic.code = code;
	diagnostic.line = location.line;
	diagnostic.column = location.column;
	diagnostic.message = std::move(message);
	diagnostics_.push_back(std::move(diagnostic));
}

Type* TypeTable::define(TypeKind kind, const Identifier& name, SourceLocation location) {
	std::string key = toUpper(name.name);
	if (auto existing = named_.find(key); existing != named_.end()) {
		error("M002", name.location, name.name + " is already defined on line "
			+ std::to_string(existing->second->location.line));
		return nullptr;
	}
	auto type = std::make_unique<Type>();
	type->kind = kind;
	type->name = name.name;
	type->location = location;
	Type* result = type.get();
	owned_.push_back(std::move(type));
	named_[key] = result;
	defined_.push_back(result);
	return result;
}

void TypeTable::build(const Program& program) {
	// Pass 1: register every name, so definitions can refer to each other in
	// any order (spec 3: definitions are hoisted).
	std::vector<std::pair<Type*, const TypeDef*>> typeDefs;
	for (const TypeDef& def : program.types) {
		if (def.name.name.empty()) {
			continue; // already reported as a syntax error
		}
		TypeKind kind = std::visit(Overloaded{
			[](const EnumTypeDef&) { return TypeKind::Enum; },
			[](const PointerTypeDef&) { return TypeKind::Pointer; },
			[](const SetTypeDef&) { return TypeKind::Set; },
			[](const RecordTypeDef&) { return TypeKind::Record; },
		}, def.body);
		if (Type* type = define(kind, def.name, def.location)) {
			typeDefs.emplace_back(type, &def);
		}
	}

	std::vector<std::pair<Type*, const ClassDef*>> classDefs;
	for (const ClassDef& def : program.classes) {
		if (def.name.name.empty()) {
			continue;
		}
		if (Type* type = define(TypeKind::Class, def.name, def.location)) {
			type->classDef = &def;
			classDefs.emplace_back(type, &def);
		}
	}

	// Enumerated values become constants in the shared namespace (spec 5.4, 5.8).
	for (auto& [type, def] : typeDefs) {
		const auto* enumDef = std::get_if<EnumTypeDef>(&def->body);
		if (!enumDef) {
			continue;
		}
		for (const Identifier& value : enumDef->values) {
			std::string key = toUpper(value.name);
			if (auto existing = enumConstants_.find(key); existing != enumConstants_.end()) {
				error("M002", value.location, value.name + " is already a value of the enumerated type "
					+ existing->second.type->name);
			}
			else if (auto named = named_.find(key); named != named_.end()) {
				error("M002", value.location, value.name + " is already the name of a type, defined on line "
					+ std::to_string(named->second->location.line));
			}
			else {
				enumConstants_[key] = { type, static_cast<int>(type->enumValues.size()) };
				type->enumValues.push_back(value.name);
			}
		}
	}

	// Pass 2: resolve what each TYPE refers to.
	for (auto& [type, def] : typeDefs) {
		std::visit(Overloaded{
			[](const EnumTypeDef&) {},
			[&](const PointerTypeDef& pointer) {
				type->element = resolve(pointer.target);
			},
			[&](const SetTypeDef& set) {
				type->element = resolve(set.element);
				if (type->element && !type->element->isAtomic() && type->element->kind != TypeKind::Enum) {
					error("M004", set.element.location, "A SET can only hold simple values such as CHAR, INTEGER or an enumerated type, not "
						+ describeType(type->element));
				}
			},
			[&](const RecordTypeDef& record) {
				for (const DeclareStmt& field : record.fields) {
					if (type->findField(field.name.name) >= 0) {
						error("M002", field.name.location, "The record " + type->name + " already has a field called " + field.name.name);
						continue;
					}
					type->fields.push_back({ field.name.name, resolve(field.type), &field.type,
						Visibility::Public, type, field.name.location });
				}
			},
		}, def->body);
	}

	// Pass 3: classes, each after its parent so inherited attributes come first.
	std::map<const Type*, int> state;
	for (auto& [type, def] : classDefs) {
		completeClass(type, *def, state);
	}

	checkRecordCycles();
}

// state: 0 not started, 1 in progress, 2 done.
void TypeTable::completeClass(Type* classType, const ClassDef& def, std::map<const Type*, int>& state) {
	if (state[classType] != 0) {
		return;
	}
	state[classType] = 1;

	if (def.parent) {
		auto found = named_.find(toUpper(def.parent->name));
		if (found == named_.end()) {
			error("M001", def.parent->location, "Unknown class " + def.parent->name + ", so " + classType->name + " cannot inherit from it");
		}
		else if (found->second->kind != TypeKind::Class) {
			error("M004", def.parent->location, def.parent->name + " is not a class, so " + classType->name + " cannot inherit from it");
		}
		else if (found->second == classType) {
			error("M004", def.parent->location, "A class cannot inherit from itself");
		}
		else if (state[found->second] == 1) {
			error("M004", def.parent->location, classType->name + " cannot inherit from " + found->second->name
				+ ", because " + found->second->name + " already inherits from " + classType->name + " (directly or indirectly)");
		}
		else {
			Type* parent = found->second;
			completeClass(parent, *parent->classDef, state);
			classType->parent = parent;
			classType->fields = parent->fields;
		}
	}

	for (const Attribute& attribute : def.attributes) {
		int existing = classType->findField(attribute.name.name);
		if (existing >= 0) {
			const Field& field = classType->fields[static_cast<size_t>(existing)];
			error("M002", attribute.name.location, classType->name + " already has an attribute called " + attribute.name.name
				+ (field.owner != classType ? " (inherited from " + field.owner->name + ")" : ""));
			continue;
		}
		classType->fields.push_back({ attribute.name.name, resolve(attribute.type), &attribute.type,
			attribute.visibility, classType, attribute.name.location });
	}

	for (const Subroutine& method : def.methods) {
		if (method.name.name.empty()) {
			continue;
		}
		auto duplicate = std::find_if(classType->methods.begin(), classType->methods.end(),
			[&](const Method& m) { return equalsIgnoreCase(m.name, method.name.name); });
		if (duplicate != classType->methods.end()) {
			error("M002", method.name.location, classType->name + " already has a method called " + duplicate->name
				+ " on line " + std::to_string(duplicate->definition->location.line));
			continue;
		}
		if (classType->findField(method.name.name) >= 0) {
			error("M002", method.name.location, classType->name + " already has an attribute called " + method.name.name);
			continue;
		}
		classType->methods.push_back({ method.name.name, &method, classType });
	}

	state[classType] = 2;
}

// Spec 5.6: a record cannot contain itself, directly or through arrays and
// other records, as it would be infinitely large. Pointers and objects are
// references, so they break the chain.
void TypeTable::checkRecordCycles() {
	for (const Type* record : defined_) {
		if (record->kind != TypeKind::Record) {
			continue;
		}
		std::vector<const Type*> visited;
		auto contains = [&](auto& self, const Type* outer) -> bool {
			for (const Field& field : outer->fields) {
				const Type* type = field.type;
				while (type && type->kind == TypeKind::Array) {
					type = type->element;
				}
				if (!type || type->kind != TypeKind::Record) {
					continue;
				}
				if (type == record) {
					return true;
				}
				if (std::find(visited.begin(), visited.end(), type) == visited.end()) {
					visited.push_back(type);
					if (self(self, type)) {
						return true;
					}
				}
			}
			return false;
		};
		if (contains(contains, record)) {
			error("M004", record->location, "The record " + record->name
				+ " contains itself, so it would be infinitely large. Use a pointer type for the link instead");
		}
	}
}

const Type* TypeTable::find(std::string_view name) const {
	auto found = named_.find(toUpper(name));
	return found == named_.end() ? nullptr : found->second;
}

std::optional<TypeTable::EnumConstant> TypeTable::findEnumValue(std::string_view name) const {
	auto found = enumConstants_.find(toUpper(name));
	if (found == enumConstants_.end()) {
		return std::nullopt;
	}
	return found->second;
}

const Type* TypeTable::resolve(const TypeSpec& spec) {
	return std::visit(Overloaded{
		[](BuiltinType builtin) { return builtinType(builtin); },
		[&](const NamedType& named) -> const Type* {
			if (const Type* type = find(named.name)) {
				return type;
			}
			if (auto constant = findEnumValue(named.name)) {
				error("M001", spec.location, named.name + " is a value of the enumerated type "
					+ constant->type->name + ", not a type");
			}
			else {
				error("M001", spec.location, "Unknown type " + named.name
					+ ". Define it with TYPE or CLASS, or use INTEGER, REAL, CHAR, STRING, BOOLEAN or DATE");
			}
			return nullptr;
		},
		[&](const ArrayType& array) -> const Type* {
			const Type* element = array.element ? resolve(*array.element) : nullptr;
			if (!element) {
				return nullptr;
			}
			return arrayOf(element, static_cast<int>(array.bounds.size()));
		},
	}, spec.node);
}

const Type* TypeTable::arrayOf(const Type* element, int dimensions) {
	auto key = std::make_pair(element, dimensions);
	if (auto found = arrays_.find(key); found != arrays_.end()) {
		return found->second;
	}
	auto type = std::make_unique<Type>();
	type->kind = TypeKind::Array;
	type->element = element;
	type->dimensions = dimensions;
	type->name = (dimensions > 1 ? std::to_string(dimensions) + "D ARRAY OF " : "ARRAY OF ") + element->name;
	const Type* result = type.get();
	owned_.push_back(std::move(type));
	arrays_[key] = result;
	return result;
}

const Type* TypeTable::pointerTo(const Type* target) {
	if (auto found = pointers_.find(target); found != pointers_.end()) {
		return found->second;
	}
	auto type = std::make_unique<Type>();
	type->kind = TypeKind::Pointer;
	type->element = target;
	type->name = "^" + target->name;
	const Type* result = type.get();
	owned_.push_back(std::move(type));
	pointers_[target] = result;
	return result;
}

std::string TypeTable::dump() const {
	std::string out;
	for (const Type* type : defined_) {
		switch (type->kind) {
		case TypeKind::Enum: {
			out += "TYPE " + type->name + " = (";
			for (size_t i = 0; i < type->enumValues.size(); ++i) {
				out += (i > 0 ? ", " : "") + type->enumValues[i];
			}
			out += ")\n";
			break;
		}
		case TypeKind::Pointer:
			out += "TYPE " + type->name + " = ^" + typeName(type->element) + "\n";
			break;
		case TypeKind::Set:
			out += "TYPE " + type->name + " = SET OF " + typeName(type->element) + "\n";
			break;
		case TypeKind::Record:
			out += "TYPE " + type->name + "\n";
			for (const Field& field : type->fields) {
				out += "  " + field.name + " : " + typeName(field.type) + "\n";
			}
			break;
		case TypeKind::Class:
			out += "CLASS " + type->name;
			if (type->parent) {
				out += " INHERITS " + type->parent->name;
			}
			out += "\n";
			for (const Field& field : type->fields) {
				out += std::string("  ") + (field.visibility == Visibility::Private ? "PRIVATE " : "PUBLIC ")
					+ field.name + " : " + typeName(field.type);
				if (field.owner != type) {
					out += "  (from " + field.owner->name + ")";
				}
				out += "\n";
			}
			for (const Type* owner = type; owner; owner = owner->parent) {
				for (const Method& method : owner->methods) {
					if (type->findMethod(method.name) != &method) {
						continue; // overridden lower down
					}
					const Subroutine& sub = *method.definition;
					out += std::string("  ") + (sub.visibility == Visibility::Private ? "PRIVATE " : "PUBLIC ")
						+ (sub.isFunction ? "FUNCTION " : "PROCEDURE ") + method.name;
					if (owner != type) {
						out += "  (from " + owner->name + ")";
					}
					out += "\n";
				}
			}
			break;
		default:
			break;
		}
	}
	return out;
}

// ---------- type rules ----------

bool sameType(const Type* a, const Type* b) {
	if (a == b) {
		return a != nullptr;
	}
	if (!a || !b || a->kind != b->kind) {
		return false;
	}
	if (a->kind == TypeKind::Array) {
		return a->dimensions == b->dimensions && sameType(a->element, b->element);
	}
	if (a->kind == TypeKind::Pointer) {
		return sameType(a->element, b->element);
	}
	return false;
}

bool isAssignable(const Type* target, const Type* source) {
	if (!target || !source || sameType(target, source)) {
		return true;
	}
	if (target->kind == TypeKind::Real && source->kind == TypeKind::Integer) {
		return true;
	}
	if (target->kind == TypeKind::String && source->kind == TypeKind::Char) {
		return true;
	}
	if (target->kind == TypeKind::Class && source->kind == TypeKind::Class) {
		return source->isA(target);
	}
	if (target->isOpenArray() && source->kind == TypeKind::Array) {
		return sameType(target->element, source->element);
	}
	return false;
}

std::string assignmentError(const Type* target, const Type* source) {
	if (isAssignable(target, source)) {
		return {};
	}
	std::string message = "Cannot use " + describeType(source) + " where " + describeType(target) + " is expected";
	if (target->kind == TypeKind::Integer && source->kind == TypeKind::Real) {
		message += ". Use INT() to remove the fractional part";
	}
	else if (target->kind == TypeKind::Char && source->kind == TypeKind::String) {
		message += ". A STRING cannot become a CHAR, even if it is one character long";
	}
	else if (target->isNumeric() && source->isText()) {
		message += ". Use STR_TO_NUM to turn text into a number";
	}
	else if (target->isText() && source->isNumeric()) {
		message += ". Use NUM_TO_STR to turn a number into text";
	}
	else if (target->kind == TypeKind::Array && source->kind == TypeKind::Array) {
		message += target->dimensions != source->dimensions && !target->isOpenArray()
			? ". The arrays have different numbers of dimensions"
			: ". The element types must match exactly";
	}
	else if (target->kind == TypeKind::Class && source->kind == TypeKind::Class) {
		message += ", because " + source->name + " does not inherit from " + target->name;
	}
	return message;
}

std::string_view operatorSymbol(BinaryOp op) {
	switch (op) {
	case BinaryOp::Or:           return "OR";
	case BinaryOp::And:          return "AND";
	case BinaryOp::Equal:        return "=";
	case BinaryOp::NotEqual:     return "<>";
	case BinaryOp::Less:         return "<";
	case BinaryOp::Greater:      return ">";
	case BinaryOp::LessEqual:    return "<=";
	case BinaryOp::GreaterEqual: return ">=";
	case BinaryOp::In:           return "IN";
	case BinaryOp::Concat:       return "&";
	case BinaryOp::Add:          return "+";
	case BinaryOp::Subtract:     return "-";
	case BinaryOp::Multiply:     return "*";
	case BinaryOp::Divide:       return "/";
	case BinaryOp::IntDiv:       return "DIV";
	case BinaryOp::Mod:          return "MOD";
	}
	return "?";
}

OperatorCheck checkUnary(UnaryOp op, const Type* operand) {
	if (op == UnaryOp::Not) {
		if (operand && operand->kind != TypeKind::Boolean) {
			return { nullptr, "NOT needs TRUE or FALSE, but this is " + describeType(operand) };
		}
		return { booleanType(), {} };
	}
	if (operand && !operand->isNumeric()) {
		return { nullptr, std::string(op == UnaryOp::Negate ? "A minus sign" : "A plus sign")
			+ " needs a number, but this is " + describeType(operand) };
	}
	return { operand, {} };
}

// Spec 4.4, 6.1, 6.2.
OperatorCheck checkBinary(BinaryOp op, const Type* left, const Type* right) {
	std::string symbol(operatorSymbol(op));
	auto ok = [](const Type* result) { return OperatorCheck{ result, {} }; };
	auto fail = [](std::string message) { return OperatorCheck{ nullptr, std::move(message) }; };
	const std::pair<const Type*, const char*> sides[] = { { left, "left" }, { right, "right" } };

	switch (op) {
	case BinaryOp::Or:
	case BinaryOp::And:
		for (auto [type, side] : sides) {
			if (type && type->kind != TypeKind::Boolean) {
				return fail(symbol + " needs TRUE or FALSE on both sides, but the " + side + " side is " + describeType(type));
			}
		}
		return ok(booleanType());

	case BinaryOp::Equal:
	case BinaryOp::NotEqual:
	case BinaryOp::Less:
	case BinaryOp::Greater:
	case BinaryOp::LessEqual:
	case BinaryOp::GreaterEqual: {
		std::string error = comparisonError(op, left, right);
		return error.empty() ? ok(booleanType()) : fail(std::move(error));
	}

	case BinaryOp::In:
		if (right && right->kind != TypeKind::Set) {
			return fail("IN needs a set on its right, but this is " + describeType(right));
		}
		if (left && right && !comparisonError(BinaryOp::Equal, left, right->element).empty()) {
			return fail("Cannot look for " + describeType(left) + " in " + right->name
				+ ", which holds " + typeName(right->element) + " values");
		}
		return ok(booleanType());

	case BinaryOp::Concat:
		for (auto [type, side] : sides) {
			if (type && !type->isText()) {
				std::string message = "& joins text, but the " + std::string(side) + " side is " + describeType(type);
				if (type->isNumeric()) {
					message += ". Use NUM_TO_STR to turn a number into text";
				}
				return fail(std::move(message));
			}
		}
		return ok(stringType());

	case BinaryOp::Add:
	case BinaryOp::Subtract:
		// Spec 5.4: an enumerated value plus or minus an INTEGER.
		if (left && left->kind == TypeKind::Enum) {
			if (!right || right->kind == TypeKind::Integer) return ok(left);
			return fail("Only an INTEGER can be added to or subtracted from " + describeType(left));
		}
		if (op == BinaryOp::Add && right && right->kind == TypeKind::Enum) {
			if (!left || left->kind == TypeKind::Integer) return ok(right);
			return fail("Only an INTEGER can be added to " + describeType(right));
		}
		if (op == BinaryOp::Add && ((left && left->isText()) || (right && right->isText()))) {
			return fail("+ adds numbers. To join text use &, and NUM_TO_STR to turn a number into text");
		}
		[[fallthrough]];
	case BinaryOp::Multiply:
	case BinaryOp::Divide:
		for (auto [type, side] : sides) {
			if (type && !type->isNumeric()) {
				std::string message = symbol + " needs numbers, but the " + side + " side is " + describeType(type);
				if (type->isText()) {
					message += ". Use STR_TO_NUM to turn text into a number";
				}
				return fail(std::move(message));
			}
		}
		if (op == BinaryOp::Divide) {
			return ok(realType());
		}
		if ((left && left->kind == TypeKind::Real) || (right && right->kind == TypeKind::Real)) {
			return ok(realType());
		}
		return ok(left && right ? integerType() : nullptr);

	case BinaryOp::IntDiv:
	case BinaryOp::Mod:
		for (auto [type, side] : sides) {
			if (type && type->kind != TypeKind::Integer) {
				std::string message = symbol + " only works on INTEGERs, but the " + side + " side is " + describeType(type);
				if (type->kind == TypeKind::Real) {
					message += ". Use INT() to convert it first";
				}
				return fail(std::move(message));
			}
		}
		return ok(integerType());
	}
	return fail(capitalise("unknown operator " + symbol));
}
