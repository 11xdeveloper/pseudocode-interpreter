#include "Value.h"

#include <charconv>
#include <cmath>
#include <cstdio>

#include "RuntimeError.h"

namespace {

// Largest number of elements one array may have. Each element is a cell of
// roughly 80 bytes, so this keeps a mistyped bound from exhausting memory.
constexpr std::uint64_t MaxArrayElements = 4'000'000;

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string toUpper(std::string_view text) {
	std::string result(text);
	for (char& c : result) {
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}
	return result;
}

void copyArrayInto(ArrayValue& target, const ArrayValue& source) {
	if (&target == &source) {
		return;
	}
	bool sameShape = target.bounds.size() == source.bounds.size();
	for (size_t i = 0; sameShape && i < target.bounds.size(); ++i) {
		sameShape = target.bounds[i].size() == source.bounds[i].size();
	}
	if (!sameShape) {
		throw RuntimeError("R002", "Cannot copy an array " + source.boundsText() + " into an array " + target.boundsText()
			+ ": every dimension must be the same size");
	}
	for (size_t i = 0; i < target.elements.size(); ++i) {
		storeValue(*target.elements[i], source.elements[i]->value);
	}
}

void copyRecordInto(RecordValue& target, const RecordValue& source) {
	if (&target == &source) {
		return;
	}
	for (size_t i = 0; i < target.fields.size(); ++i) {
		storeValue(*target.fields[i], source.fields[i]->value);
	}
}

// Zero value of a type for --defaults (spec 5.1).
Value defaultValue(const Type* type) {
	switch (type->kind) {
	case TypeKind::Integer: return std::int64_t{ 0 };
	case TypeKind::Real:    return 0.0;
	case TypeKind::Char:    return ' ';
	case TypeKind::String:  return std::string();
	case TypeKind::Boolean: return false;
	case TypeKind::Date:    return Date{};
	case TypeKind::Enum:
		if (!type->enumValues.empty()) return EnumValue{ type, 0 };
		return {};
	case TypeKind::Set:     return SetRef(std::make_shared<const SetValue>(SetValue{ type, {} }));
	default:                return {};
	}
}

}

// ---------- pointers and arrays ----------

bool PointerValue::isNull() const {
	std::weak_ptr<Cell> empty;
	return !target.owner_before(empty) && !empty.owner_before(target);
}

bool PointerValue::sameTarget(const PointerValue& other) const {
	return !target.owner_before(other.target) && !other.target.owner_before(target);
}

const CellPtr& ArrayValue::element(const std::vector<std::int64_t>& indices, std::string_view name) const {
	if (indices.size() != bounds.size()) {
		std::string needed = std::to_string(bounds.size()) + (bounds.size() == 1 ? " index" : " indices");
		throw RuntimeError("R002", std::string(name) + " has " + std::to_string(bounds.size())
			+ (bounds.size() == 1 ? " dimension" : " dimensions") + ", so it needs " + needed
			+ ", but " + std::to_string(indices.size()) + (indices.size() == 1 ? " was" : " were") + " given");
	}
	size_t offset = 0;
	for (size_t i = 0; i < indices.size(); ++i) {
		const ArrayBound& bound = bounds[i];
		if (indices[i] < bound.lower || indices[i] > bound.upper) {
			throw RuntimeError("R003", "Index " + std::to_string(indices[i]) + " is outside the bounds of "
				+ std::string(name) + " " + boundsText());
		}
		offset = offset * static_cast<size_t>(bound.size()) + static_cast<size_t>(indices[i] - bound.lower);
	}
	return elements[offset];
}

std::string ArrayValue::boundsText() const {
	std::string text = "[";
	for (size_t i = 0; i < bounds.size(); ++i) {
		if (i > 0) text += ", ";
		text += std::to_string(bounds[i].lower) + ':' + std::to_string(bounds[i].upper);
	}
	return text + ']';
}

// ---------- types, copying and storing ----------

const Type* typeOf(const Value& value) {
	return std::visit(Overloaded{
		[](std::monostate) -> const Type* { return nullptr; },
		[](std::int64_t) { return integerType(); },
		[](double) { return realType(); },
		[](char) { return charType(); },
		[](const std::string&) { return stringType(); },
		[](bool) { return booleanType(); },
		[](const Date&) { return dateType(); },
		[](const EnumValue& v) { return v.type; },
		[](const PointerValue& v) { return v.type; },
		[](const ArrayRef& v) { return v ? v->type : nullptr; },
		[](const RecordRef& v) { return v ? v->type : nullptr; },
		[](const ObjectRef& v) { return v ? v->type : nullptr; },
		[](const SetRef& v) { return v ? v->type : nullptr; },
	}, value);
}

bool isUnassigned(const Value& value) {
	return std::holds_alternative<std::monostate>(value);
}

Value copyValue(const Value& value) {
	if (const auto* array = std::get_if<ArrayRef>(&value); array && *array) {
		auto copy = std::make_shared<ArrayValue>();
		copy->type = (*array)->type;
		copy->bounds = (*array)->bounds;
		copy->elements.reserve((*array)->elements.size());
		for (const CellPtr& element : (*array)->elements) {
			copy->elements.push_back(std::make_shared<Cell>(Cell{ element->type, copyValue(element->value) }));
		}
		return copy;
	}
	if (const auto* record = std::get_if<RecordRef>(&value); record && *record) {
		auto copy = std::make_shared<RecordValue>();
		copy->type = (*record)->type;
		copy->fields.reserve((*record)->fields.size());
		for (const CellPtr& field : (*record)->fields) {
			copy->fields.push_back(std::make_shared<Cell>(Cell{ field->type, copyValue(field->value) }));
		}
		return copy;
	}
	return value;
}

Value convertTo(const Type* target, Value value) {
	if (!target || isUnassigned(value)) {
		return value;
	}
	const Type* source = typeOf(value);
	if (!source) {
		// A null object can go anywhere an object can.
		if (target->kind == TypeKind::Class) {
			return value;
		}
		throw RuntimeError("R002", "Cannot use a null object where " + describeType(target) + " is expected");
	}
	if (target->kind == TypeKind::Real && source->kind == TypeKind::Integer) {
		return static_cast<double>(std::get<std::int64_t>(value));
	}
	if (target->kind == TypeKind::String && source->kind == TypeKind::Char) {
		return std::string(1, std::get<char>(value));
	}
	if (!isAssignable(target, source)) {
		throw RuntimeError("R002", assignmentError(target, source));
	}
	if (target->kind == TypeKind::Pointer) {
		// ^Counter has the structural type ^INTEGER; take on the declared name.
		std::get<PointerValue>(value).type = target;
	}
	return value;
}

void storeValue(Cell& cell, const Value& value) {
	if (!cell.type) {
		cell.type = typeOf(value);
		cell.value = copyValue(value);
		return;
	}
	Value converted = convertTo(cell.type, value);
	if (auto* target = std::get_if<ArrayRef>(&cell.value); target && *target) {
		if (auto* source = std::get_if<ArrayRef>(&converted); source && *source) {
			copyArrayInto(**target, **source);
			return;
		}
	}
	if (auto* target = std::get_if<RecordRef>(&cell.value); target && *target) {
		if (auto* source = std::get_if<RecordRef>(&converted); source && *source) {
			copyRecordInto(**target, **source);
			return;
		}
	}
	cell.value = copyValue(converted);
}

// ---------- creating values ----------

Value makeValue(const Type* type, const TypeSpec* spec, const BoundEvaluator& evaluateBound, bool useDefaults) {
	if (!type) {
		return {};
	}
	switch (type->kind) {
	case TypeKind::Array: {
		const ArrayType* arraySpec = spec ? std::get_if<ArrayType>(&spec->node) : nullptr;
		if (!arraySpec || arraySpec->bounds.empty()) {
			throw RuntimeError("R002", "Cannot create " + describeType(type) + " without its bounds");
		}
		auto array = std::make_shared<ArrayValue>();
		array->type = type;
		std::uint64_t total = 1;
		for (const ArrayBounds& bounds : arraySpec->bounds) {
			std::int64_t lower = evaluateBound(*bounds.lower);
			std::int64_t upper = evaluateBound(*bounds.upper);
			if (upper < lower) {
				throw RuntimeError("R003", "The array bounds [" + std::to_string(lower) + ':' + std::to_string(upper)
					+ "] are invalid: the upper bound must not be less than the lower bound");
			}
			std::uint64_t size = static_cast<std::uint64_t>(upper) - static_cast<std::uint64_t>(lower) + 1;
			if (size > MaxArrayElements || total * size > MaxArrayElements) {
				throw RuntimeError("R003", "This array would have more than " + std::to_string(MaxArrayElements)
					+ " elements, which is too large");
			}
			total *= size;
			array->bounds.push_back({ lower, upper });
		}
		array->elements.reserve(static_cast<size_t>(total));
		for (std::uint64_t i = 0; i < total; ++i) {
			array->elements.push_back(makeCell(type->element, arraySpec->element.get(), evaluateBound, useDefaults));
		}
		return array;
	}
	case TypeKind::Record: {
		auto record = std::make_shared<RecordValue>();
		record->type = type;
		record->fields.reserve(type->fields.size());
		for (const Field& field : type->fields) {
			record->fields.push_back(makeCell(field.type, field.spec, evaluateBound, useDefaults));
		}
		return record;
	}
	case TypeKind::Class:
		return ObjectRef{};
	case TypeKind::Pointer:
		return PointerValue{ type, {} };
	default:
		return useDefaults ? defaultValue(type) : Value{};
	}
}

CellPtr makeCell(const Type* type, const TypeSpec* spec, const BoundEvaluator& evaluateBound, bool useDefaults) {
	return std::make_shared<Cell>(Cell{ type, makeValue(type, spec, evaluateBound, useDefaults) });
}

ObjectRef makeObject(const Type* classType, const BoundEvaluator& evaluateBound, bool useDefaults) {
	auto object = std::make_shared<ObjectValue>();
	object->type = classType;
	object->attributes.reserve(classType->fields.size());
	for (const Field& field : classType->fields) {
		object->attributes.push_back(makeCell(field.type, field.spec, evaluateBound, useDefaults));
	}
	return object;
}

CellPtr dereference(const PointerValue& pointer) {
	if (pointer.isNull()) {
		throw RuntimeError("R008", "This pointer has not been given an address yet");
	}
	CellPtr cell = pointer.target.lock();
	if (!cell) {
		throw RuntimeError("R008", "This pointer refers to a variable that no longer exists, "
			"such as a local variable of a subroutine that has finished");
	}
	return cell;
}

// ---------- text ----------

std::string formatReal(double value) {
	char buffer[64];
	auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
	std::string text(buffer, result.ptr);
	if (!std::isfinite(value) || text.find('.') != std::string::npos) {
		return text;
	}
	size_t exponent = text.find('e');
	if (exponent == std::string::npos) {
		return text + ".0";
	}
	return text.insert(exponent, ".0"); // 1e+20 becomes 1.0e+20
}

std::string formatDate(const Date& date) {
	char buffer[16];
	std::snprintf(buffer, sizeof buffer, "%02d/%02d/%04d", date.day, date.month, date.year);
	return buffer;
}

std::string formatValue(const Value& value) {
	return std::visit(Overloaded{
		[](std::monostate) -> std::string {
			throw RuntimeError("R001", "This has not been given a value yet");
		},
		[](std::int64_t v) { return std::to_string(v); },
		[](double v) { return formatReal(v); },
		[](char v) { return std::string(1, v); },
		[](const std::string& v) { return v; },
		[](bool v) { return std::string(v ? "TRUE" : "FALSE"); },
		[](const Date& v) { return formatDate(v); },
		[](const EnumValue& v) { return v.type->enumValues[static_cast<size_t>(v.ordinal)]; },
		[](const PointerValue&) -> std::string {
			throw RuntimeError("R002", "A pointer cannot be output. Put ^ after it to use the value it points to");
		},
		[](const ArrayRef&) -> std::string {
			throw RuntimeError("R002", "A whole array cannot be output. Output its elements one at a time");
		},
		[](const RecordRef&) -> std::string {
			throw RuntimeError("R002", "A whole record cannot be output. Output its fields one at a time");
		},
		[](const ObjectRef&) -> std::string {
			throw RuntimeError("R002", "An object cannot be output. Output its attributes or the results of its methods");
		},
		[](const SetRef&) -> std::string {
			throw RuntimeError("R002", "A set cannot be output");
		},
	}, value);
}

bool isNumberText(std::string_view text) {
	size_t i = 0;
	if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
		++i;
	}
	size_t digitsStart = i;
	while (i < text.size() && isDigit(text[i])) {
		++i;
	}
	if (i == digitsStart) {
		return false;
	}
	if (i < text.size() && text[i] == '.') {
		size_t fractionStart = ++i;
		while (i < text.size() && isDigit(text[i])) {
			++i;
		}
		if (i == fractionStart) {
			return false;
		}
	}
	return i == text.size();
}

std::optional<Value> parseNumber(std::string_view text) {
	if (!isNumberText(text)) {
		return std::nullopt;
	}
	std::string_view digits = text.front() == '+' ? text.substr(1) : text;
	if (text.find('.') != std::string_view::npos) {
		double value = 0.0;
		std::from_chars(digits.data(), digits.data() + digits.size(), value);
		return value;
	}
	std::int64_t value = 0;
	auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
	if (result.ec == std::errc::result_out_of_range) {
		throw RuntimeError("R005", "The number " + std::string(text) + " is too large for an INTEGER");
	}
	return value;
}

std::optional<Date> parseDate(std::string_view text) {
	size_t pos = 0;
	auto number = [&](size_t minDigits, size_t maxDigits) -> std::optional<int> {
		size_t start = pos;
		int value = 0;
		while (pos < text.size() && isDigit(text[pos]) && pos - start < maxDigits) {
			value = value * 10 + (text[pos] - '0');
			++pos;
		}
		if (pos - start < minDigits) {
			return std::nullopt;
		}
		return value;
	};
	auto slash = [&] {
		if (pos < text.size() && text[pos] == '/') {
			++pos;
			return true;
		}
		return false;
	};
	auto day = number(1, 2);
	if (!day || !slash()) return std::nullopt;
	auto month = number(1, 2);
	if (!month || !slash()) return std::nullopt;
	auto year = number(4, 4);
	if (!year || pos != text.size() || !isValidDate(*day, *month, *year)) return std::nullopt;
	return Date{ *year, *month, *day };
}

Value parseInput(std::string_view text, const Type* target) {
	std::string quoted = "'" + std::string(text) + "'";
	std::string upper = toUpper(text);

	if (!target) {
		// Spec 7.2: an undeclared variable takes its type from the text.
		if (auto number = parseNumber(text)) return *number;
		if (upper == "TRUE" || upper == "FALSE") return upper == "TRUE";
		if (text.size() == 1) return text[0];
		return std::string(text);
	}

	switch (target->kind) {
	case TypeKind::Integer:
		if (isNumberText(text) && text.find('.') == std::string_view::npos) {
			return *parseNumber(text);
		}
		throw RuntimeError("R002", "An INTEGER was expected, but " + quoted + " is not a whole number");
	case TypeKind::Real:
		if (auto number = parseNumber(text)) {
			return convertTo(realType(), *number);
		}
		throw RuntimeError("R002", "A REAL was expected, but " + quoted + " is not a number");
	case TypeKind::Char:
		if (text.size() == 1) {
			return text[0];
		}
		throw RuntimeError("R002", "A CHAR was expected, so exactly one character is needed, but "
			+ (text.empty() ? std::string("nothing was entered") : quoted + " was entered"));
	case TypeKind::String:
		return std::string(text);
	case TypeKind::Boolean:
		if (upper == "TRUE" || upper == "FALSE") {
			return upper == "TRUE";
		}
		throw RuntimeError("R002", "A BOOLEAN was expected, so TRUE or FALSE is needed, but " + quoted + " was entered");
	case TypeKind::Date:
		if (auto date = parseDate(text)) {
			return *date;
		}
		throw RuntimeError("R002", "A DATE was expected, but " + quoted + " is not a valid date. Enter it as dd/mm/yyyy");
	default:
		throw RuntimeError("R002", "INPUT cannot read " + describeType(target));
	}
}
