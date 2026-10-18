#include "Environment.h"

#include <algorithm>
#include <array>

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

std::string plural(size_t count, std::string_view word) {
	return std::to_string(count) + ' ' + std::string(word) + (count == 1 ? "" : "s");
}

std::string subroutineKind(const Subroutine& subroutine) {
	return subroutine.isFunction ? "function" : "procedure";
}

// "procedure Swap", "function Max", "method Cat.Speak".
std::string describeTarget(const CallTarget& target) {
	if (!target.subroutine) {
		return "the attribute initialisers of " + typeName(target.methodClass);
	}
	if (target.methodClass) {
		return "method " + target.methodClass->name + "." + target.subroutine->name.name;
	}
	return subroutineKind(*target.subroutine) + " " + target.subroutine->name.name;
}

bool canSee(const Field& field, const Type* fromClass) {
	return field.visibility == Visibility::Public || field.owner == fromClass;
}

bool canSee(const Method& method, const Type* fromClass) {
	return method.definition->visibility == Visibility::Public || method.owner == fromClass;
}

}

bool isBuiltinFunction(std::string_view name) {
	static constexpr std::array<std::string_view, 25> names = {
		"LENGTH", "LEFT", "RIGHT", "MID", "LCASE", "UCASE", "TO_LOWER", "TO_UPPER",
		"NUM_TO_STR", "STR_TO_NUM", "IS_NUM", "ASC", "CHR",
		"INT", "RAND", "ROUND", "MOD", "DIV",
		"DAY", "MONTH", "YEAR", "DAYINDEX", "SETDATE", "NOW",
		"EOF",
	};
	return std::find(names.begin(), names.end(), toUpper(name)) != names.end();
}

// ---------- Scope ----------

Variable* Scope::find(std::string_view name) {
	auto found = variables_.find(toUpper(name));
	return found == variables_.end() ? nullptr : &found->second;
}

const Variable* Scope::find(std::string_view name) const {
	auto found = variables_.find(toUpper(name));
	return found == variables_.end() ? nullptr : &found->second;
}

Variable& Scope::set(Variable variable) {
	std::string key = toUpper(variable.name);
	return variables_.insert_or_assign(std::move(key), std::move(variable)).first->second;
}

// ---------- setup ----------

Environment::Environment(const Program& program, TypeTable& types, Options options)
	: types_(types), options_(options) {
	frames_.push_back(std::make_unique<Frame>());

	for (const Type* type : types_.definedTypes()) {
		if (isBuiltinFunction(type->name)) {
			error("M002", type->location, type->name + " is the name of a built-in function, so it cannot be used for a "
				+ std::string(type->kind == TypeKind::Class ? "class" : "type"));
		}
	}
	for (const Subroutine& subroutine : program.subroutines) {
		registerSubroutine(subroutine);
		resolveSignature(subroutine);
	}
	for (const ClassDef& classDef : program.classes) {
		for (const Subroutine& method : classDef.methods) {
			resolveSignature(method);
		}
	}
}

void Environment::error(const char* code, SourceLocation location, std::string message) {
	Diagnostic diagnostic;
	diagnostic.severity = Severity::Error;
	diagnostic.phase = Phase::Semantic;
	diagnostic.code = code;
	diagnostic.line = location.line;
	diagnostic.column = location.column;
	diagnostic.message = std::move(message);
	diagnostics_.push_back(std::move(diagnostic));
}

void Environment::registerSubroutine(const Subroutine& subroutine) {
	const Identifier& name = subroutine.name;
	if (name.name.empty()) {
		return; // already reported as a syntax error
	}
	if (isBuiltinFunction(name.name)) {
		error("M002", name.location, name.name + " is a built-in function, so it cannot be redefined");
		return;
	}
	if (const Subroutine* existing = findSubroutine(name.name)) {
		error("M002", name.location, "There is already a " + subroutineKind(*existing) + " called "
			+ existing->name.name + " on line " + std::to_string(existing->location.line));
		return;
	}
	if (const Type* type = types_.find(name.name)) {
		error("M002", name.location, name.name + " is already the name of a "
			+ std::string(type->kind == TypeKind::Class ? "class" : "type") + ", defined on line "
			+ std::to_string(type->location.line));
		return;
	}
	if (auto constant = types_.findEnumValue(name.name)) {
		error("M002", name.location, name.name + " is already a value of the enumerated type " + constant->type->name);
		return;
	}
	subroutines_[toUpper(name.name)] = &subroutine;
}

void Environment::resolveSignature(const Subroutine& subroutine) {
	Signature signature;
	for (size_t i = 0; i < subroutine.parameters.size(); ++i) {
		const Parameter& parameter = subroutine.parameters[i];
		signature.parameterTypes.push_back(types_.resolve(parameter.type));

		for (size_t j = 0; j < i; ++j) {
			if (toUpper(subroutine.parameters[j].name.name) == toUpper(parameter.name.name)) {
				error("M002", parameter.name.location, "There is already a parameter called "
					+ subroutine.parameters[j].name.name + " in " + subroutine.name.name);
				break;
			}
		}
		if (subroutine.isFunction && parameter.mode == PassMode::ByReference) {
			error("M006", parameter.name.location, "A FUNCTION cannot have BYREF parameters. Make " + subroutine.name.name
				+ " a PROCEDURE, or pass " + parameter.name.name + " BYVAL and RETURN the result");
		}
	}
	if (subroutine.returnType) {
		signature.returnType = types_.resolve(*subroutine.returnType);
	}
	signatures_[&subroutine] = std::move(signature);
}

// ---------- names ----------

std::optional<NameBinding> Environment::lookup(std::string_view name) const {
	const Frame& frame = *frames_.back();
	const Scope& globals = frames_.front()->locals;

	if (&frame.locals != &globals) {
		if (const Variable* local = frame.locals.find(name)) {
			return NameBinding{ .kind = NameBinding::Kind::Variable, .name = local->name, .cell = local->cell,
				.isConstant = local->isConstant };
		}
	}

	// Inside a method, attributes and methods are used by bare name (spec 11.1).
	// Only those the method's class can see: a parent's PRIVATE members are
	// hidden from a subclass (spec 11.2).
	if (const Type* classType = frame.target.methodClass; classType && frame.target.self) {
		int index = classType->findField(name);
		if (index >= 0 && canSee(classType->fields[static_cast<size_t>(index)], classType)) {
			const Field& field = classType->fields[static_cast<size_t>(index)];
			return NameBinding{ .kind = NameBinding::Kind::Attribute, .name = field.name,
				.cell = frame.target.self->attributes[static_cast<size_t>(index)], .field = &field };
		}
		const Method* method = classType->findMethod(name);
		if (method && canSee(*method, classType)) {
			return NameBinding{ .kind = NameBinding::Kind::Method, .name = method->name,
				.subroutine = method->definition, .method = method };
		}
	}

	if (const Variable* global = globals.find(name)) {
		return NameBinding{ .kind = NameBinding::Kind::Variable, .name = global->name, .cell = global->cell,
			.isConstant = global->isConstant };
	}
	if (auto constant = types_.findEnumValue(name)) {
		return NameBinding{ .kind = NameBinding::Kind::EnumValue,
			.name = constant->type->enumValues[static_cast<size_t>(constant->ordinal)],
			.enumValue = EnumValue{ constant->type, constant->ordinal } };
	}
	if (const Subroutine* subroutine = findSubroutine(name)) {
		return NameBinding{ .kind = NameBinding::Kind::Subroutine, .name = subroutine->name.name, .subroutine = subroutine };
	}
	if (const Type* type = types_.find(name)) {
		return NameBinding{ .kind = NameBinding::Kind::Type, .name = type->name, .type = type };
	}
	if (isBuiltinFunction(name)) {
		return NameBinding{ .kind = NameBinding::Kind::Builtin, .name = toUpper(name) };
	}
	return std::nullopt;
}

Value Environment::read(const Identifier& name) const {
	std::optional<NameBinding> binding = lookup(name.name);
	if (!binding) {
		if (options_.strict) {
			throw RuntimeError("M001", name.name + " has not been declared");
		}
		throw RuntimeError("R001", "Variable " + name.name + " used before it was given a value");
	}

	switch (binding->kind) {
	case NameBinding::Kind::Variable:
	case NameBinding::Kind::Attribute:
		if (isUnassigned(binding->cell->value)) {
			throw RuntimeError("R001", std::string(binding->kind == NameBinding::Kind::Attribute ? "Attribute " : "Variable ")
				+ binding->name + " used before it was given a value");
		}
		return binding->cell->value;
	case NameBinding::Kind::EnumValue:
		return *binding->enumValue;
	case NameBinding::Kind::Subroutine:
	case NameBinding::Kind::Method:
		if (!binding->subroutine->isFunction) {
			throw RuntimeError("M005", binding->name + " is a procedure and does not return a value; use CALL");
		}
		throw RuntimeError("M005", binding->name + " is a function, so it needs brackets to be called, as in "
			+ binding->name + "(...)");
	case NameBinding::Kind::Type:
		throw RuntimeError("M004", binding->name + " is a type, not a value");
	case NameBinding::Kind::Builtin:
		throw RuntimeError("M005", binding->name + " is a built-in function, so it needs brackets to be called, as in "
			+ binding->name + "(...)");
	}
	return {};
}

CellPtr Environment::cellFor(const Identifier& name, const Type* typeIfNew) {
	if (std::optional<NameBinding> binding = lookup(name.name)) {
		switch (binding->kind) {
		case NameBinding::Kind::Variable:
		case NameBinding::Kind::Attribute:
			if (binding->isConstant) {
				throw RuntimeError("M007", binding->name + " is a constant, so its value cannot be changed");
			}
			return binding->cell;
		case NameBinding::Kind::EnumValue:
			throw RuntimeError("M007", binding->name + " is a value of the enumerated type "
				+ binding->enumValue->type->name + ", so it cannot be changed");
		case NameBinding::Kind::Subroutine:
		case NameBinding::Kind::Method:
			throw RuntimeError("M002", binding->name + " is a " + subroutineKind(*binding->subroutine)
				+ ", so it cannot be used as a variable"
				+ (binding->subroutine->isFunction ? ". A function gives its result with RETURN" : ""));
		case NameBinding::Kind::Type:
			throw RuntimeError("M002", binding->name + " is a type, so it cannot be used as a variable");
		case NameBinding::Kind::Builtin:
			throw RuntimeError("M002", binding->name + " is a built-in function, so it cannot be used as a variable");
		}
	}

	if (options_.strict) {
		throw RuntimeError("M001", name.name + " has not been declared. In strict mode every variable must be declared with DECLARE");
	}
	// Spec 5.1: a new variable in the current scope, typed by its first value.
	auto cell = std::make_shared<Cell>(Cell{ typeIfNew, {} });
	currentScope().set(Variable{ name.name, cell, false, name.location, nullptr });
	return cell;
}

// ---------- declarations ----------

// Spec 5.8: variables share one namespace with types, subroutines and
// classes, and built-in names are reserved.
void Environment::checkNameIsFree(const Identifier& name) const {
	if (const Subroutine* subroutine = findSubroutine(name.name)) {
		throw RuntimeError("M002", name.name + " is already the name of a " + subroutineKind(*subroutine)
			+ " on line " + std::to_string(subroutine->location.line));
	}
	if (const Type* type = types_.find(name.name)) {
		throw RuntimeError("M002", name.name + " is already the name of a "
			+ std::string(type->kind == TypeKind::Class ? "class" : "type") + " on line " + std::to_string(type->location.line));
	}
	if (auto constant = types_.findEnumValue(name.name)) {
		throw RuntimeError("M002", name.name + " is already a value of the enumerated type " + constant->type->name);
	}
	if (isBuiltinFunction(name.name)) {
		throw RuntimeError("M002", name.name + " is a built-in function, so it cannot be used as a name");
	}
}

CellPtr Environment::declare(const Identifier& name, const Type* type, const TypeSpec* spec,
	const BoundEvaluator& evaluateBound, const void* site) {
	checkNameIsFree(name);
	if (const Variable* existing = currentScope().find(name.name); existing && (existing->site != site || !site)) {
		throw RuntimeError("M002", existing->site
			? existing->name + " is already declared on line " + std::to_string(existing->location.line)
			: existing->name + " is already in use (from line " + std::to_string(existing->location.line)
				+ "), so it cannot be declared here. Put the DECLARE before its first use");
	}
	CellPtr cell = makeCell(type, spec, evaluateBound, options_.useDefaults);
	currentScope().set(Variable{ name.name, cell, false, name.location, site });
	return cell;
}

void Environment::defineConstant(const Identifier& name, const Value& value, const void* site) {
	checkNameIsFree(name);
	if (const Variable* existing = currentScope().find(name.name); existing && (existing->site != site || !site)) {
		throw RuntimeError("M002", existing->name + " is already declared on line " + std::to_string(existing->location.line));
	}
	auto cell = std::make_shared<Cell>(Cell{ typeOf(value), value });
	currentScope().set(Variable{ name.name, cell, true, name.location, site });
}

// ---------- subroutines and objects ----------

const Subroutine* Environment::findSubroutine(std::string_view name) const {
	auto found = subroutines_.find(toUpper(name));
	return found == subroutines_.end() ? nullptr : found->second;
}

const Signature& Environment::signature(const Subroutine& subroutine) const {
	return signatures_.at(&subroutine);
}

CellPtr Environment::attribute(const ObjectRef& object, const Identifier& name) const {
	if (!object) {
		throw RuntimeError("R008", "This object variable does not refer to an object yet. Create one with NEW");
	}
	const Type* classType = object->type;
	int index = classType->findField(name.name);
	if (index < 0) {
		if (classType->findMethod(name.name)) {
			throw RuntimeError("M005", name.name + " is a method of " + classType->name + ", so it needs brackets to be called");
		}
		throw RuntimeError("M001", classType->name + " has no attribute called " + name.name);
	}
	const Field& field = classType->fields[static_cast<size_t>(index)];
	if (!canSee(field, currentClass())) {
		throw RuntimeError("M009", field.name + " is PRIVATE in " + field.owner->name
			+ ", so it can only be used inside " + field.owner->name + "'s own methods");
	}
	return object->attributes[static_cast<size_t>(index)];
}

const Method& Environment::method(const ObjectRef& object, const Identifier& name) const {
	if (!object) {
		throw RuntimeError("R008", "This object variable does not refer to an object yet. Create one with NEW");
	}
	// Dispatch on the object's actual class (spec 11.4).
	const Method* found = object->type->findMethod(name.name);
	if (!found) {
		if (object->type->findField(name.name) >= 0) {
			throw RuntimeError("M005", name.name + " is an attribute of " + object->type->name + ", not a method");
		}
		throw RuntimeError("M001", object->type->name + " has no method called " + name.name);
	}
	if (!canSee(*found, currentClass())) {
		throw RuntimeError("M009", found->name + " is PRIVATE in " + found->owner->name
			+ ", so it can only be called from " + found->owner->name + "'s own methods");
	}
	return *found;
}

const Method* Environment::superMethod(const Identifier& name) const {
	const Type* classType = currentClass();
	if (!classType || !currentFrame().target.self) {
		throw RuntimeError("M008", "SUPER can only be used inside a method of a class");
	}
	if (!classType->parent) {
		throw RuntimeError("M008", classType->name + " does not inherit from another class, so it has no SUPER");
	}
	if (toUpper(name.name) == "NEW") {
		return constructorOf(classType->parent);
	}
	const Method* found = classType->parent->findMethod(name.name);
	if (!found) {
		throw RuntimeError("M001", classType->parent->name + " has no method called " + name.name);
	}
	if (!canSee(*found, classType)) {
		throw RuntimeError("M009", found->name + " is PRIVATE in " + found->owner->name
			+ ", so it can only be called from " + found->owner->name + "'s own methods");
	}
	return found;
}

const Method* Environment::constructorOf(const Type* classType) {
	for (const Method& method : classType->methods) {
		if (toUpper(method.name) == "NEW") {
			return &method;
		}
	}
	return nullptr;
}

CellPtr Environment::field(const RecordRef& record, const Identifier& name) const {
	int index = record->type->findField(name.name);
	if (index < 0) {
		throw RuntimeError("M001", "The record type " + record->type->name + " has no field called " + name.name);
	}
	return record->fields[static_cast<size_t>(index)];
}

// ---------- calls ----------

void Environment::enterCall(const CallTarget& target, std::vector<Argument> arguments, const BoundEvaluator& evaluateBound) {
	if (depth() >= options_.maxDepth) {
		std::string name = target.subroutine ? target.subroutine->name.name : typeName(target.methodClass);
		throw RuntimeError("R011", "Too many nested calls (possible infinite recursion) in " + name);
	}
	auto frame = std::make_unique<Frame>();
	frame->target = target;
	frame->callLine = frames_.back()->currentLine;
	frame->currentLine = frame->callLine;
	if (target.subroutine) {
		bindParameters(*target.subroutine, arguments, evaluateBound, frame->locals);
	}
	frames_.push_back(std::move(frame));
}

void Environment::exitCall() {
	// Destroying the frame's scope releases its local cells, so pointers to
	// them now detectably dangle (spec 5.5).
	if (frames_.size() > 1) {
		frames_.pop_back();
	}
}

// Spec 8.3.
void Environment::bindParameters(const Subroutine& subroutine, std::vector<Argument>& arguments,
	const BoundEvaluator& evaluateBound, Scope& locals) const {
	const std::vector<Parameter>& parameters = subroutine.parameters;
	const Signature& types = signature(subroutine);
	std::string kind = subroutineKind(subroutine);

	if (arguments.size() != parameters.size()) {
		throw RuntimeError("M003", "The " + kind + " " + subroutine.name.name + " needs "
			+ plural(parameters.size(), "argument") + ", but " + std::to_string(arguments.size())
			+ (arguments.size() == 1 ? " was" : " were") + " given");
	}

	for (size_t i = 0; i < parameters.size(); ++i) {
		const Parameter& parameter = parameters[i];
		const Type* type = types.parameterTypes[i];
		Argument& argument = arguments[i];
		std::string which = "parameter " + parameter.name.name + " of " + subroutine.name.name;

		if (parameter.mode == PassMode::ByReference) {
			if (subroutine.isFunction) {
				throw RuntimeError("M006", "A FUNCTION cannot have BYREF parameters");
			}
			if (!argument.cell) {
				throw RuntimeError("M006", "The BYREF " + which
					+ " needs a variable, array element or field to change, not an expression or literal");
			}
			if (argument.isConstant) {
				throw RuntimeError("M006", "A constant cannot be passed to the BYREF " + which);
			}
			if (type) {
				if (!argument.cell->type) {
					// An undeclared variable that has no value yet takes the parameter's type.
					argument.cell->type = type;
				}
				else if (!sameType(type, argument.cell->type)
					&& !(type->isOpenArray() && isAssignable(type, argument.cell->type))) {
					std::string message = "The BYREF " + which + " is " + typeName(type)
						+ ", so the argument must be exactly that type, but it is " + typeName(argument.cell->type);
					if (type->kind == TypeKind::Real && argument.cell->type->kind == TypeKind::Integer) {
						message += ". INTEGER does not become REAL for BYREF parameters";
					}
					throw RuntimeError("R002", message);
				}
				checkArrayBounds(parameter, type, argument.cell->value, evaluateBound);
			}
			locals.set(Variable{ parameter.name.name, argument.cell, false, parameter.name.location, nullptr });
			continue;
		}

		if (isUnassigned(argument.value)) {
			throw RuntimeError("R001", "The value passed for " + which + " has not been given a value");
		}
		Value converted;
		try {
			converted = convertTo(type, std::move(argument.value));
		}
		catch (const RuntimeError& error) {
			throw RuntimeError(error.code(), "Wrong argument for " + which + ": " + error.what());
		}
		checkArrayBounds(parameter, type, converted, evaluateBound);
		// A BYVAL parameter gets its own copy, arrays and records included.
		auto cell = std::make_shared<Cell>(Cell{ type, copyValue(converted) });
		locals.set(Variable{ parameter.name.name, std::move(cell), false, parameter.name.location, nullptr });
	}
}

// Spec 8.3: an array parameter written with bounds takes only arguments with
// exactly those bounds.
void Environment::checkArrayBounds(const Parameter& parameter, const Type* type, const Value& value,
	const BoundEvaluator& evaluateBound) const {
	if (!type || type->kind != TypeKind::Array || type->isOpenArray()) {
		return;
	}
	const auto* spec = std::get_if<ArrayType>(&parameter.type.node);
	const auto* array = std::get_if<ArrayRef>(&value);
	if (!spec || !array || !*array) {
		return;
	}
	ArrayValue expected;
	for (const ArrayBounds& bounds : spec->bounds) {
		expected.bounds.push_back({ evaluateBound(*bounds.lower), evaluateBound(*bounds.upper) });
	}
	bool same = expected.bounds.size() == (*array)->bounds.size();
	for (size_t i = 0; same && i < expected.bounds.size(); ++i) {
		same = expected.bounds[i].lower == (*array)->bounds[i].lower && expected.bounds[i].upper == (*array)->bounds[i].upper;
	}
	if (!same) {
		throw RuntimeError("R002", "The parameter " + parameter.name.name + " needs an array with bounds "
			+ expected.boundsText() + ", but the argument has bounds " + (*array)->boundsText()
			+ ". Write ARRAY OF " + typeName(type->element) + " to accept any bounds");
	}
}

Value Environment::returnValue(const Subroutine& function, const Value& value) const {
	const Type* type = signature(function).returnType;
	try {
		return copyValue(convertTo(type, value));
	}
	catch (const RuntimeError& error) {
		throw RuntimeError(error.code(), "The function " + function.name.name + " must return "
			+ describeType(type) + ": " + error.what());
	}
}

std::vector<std::string> Environment::callChain() const {
	std::vector<std::string> chain;
	for (size_t i = frames_.size() - 1; i > 0; --i) {
		const Frame& frame = *frames_[i];
		chain.push_back("in " + describeTarget(frame.target) + " (line " + std::to_string(frame.currentLine)
			+ "), called from line " + std::to_string(frame.callLine));
	}
	return chain;
}
