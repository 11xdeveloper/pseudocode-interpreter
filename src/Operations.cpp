#include "Operations.h"

#include <cmath>
#include <limits>

#include "RuntimeError.h"

namespace {

constexpr std::int64_t IntMin = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t IntMax = std::numeric_limits<std::int64_t>::max();

[[noreturn]] void overflow() {
	throw RuntimeError("R005", "The result is too large for an INTEGER, which must stay between "
		+ std::to_string(IntMin) + " and " + std::to_string(IntMax));
}

// Portable overflow checks, so the code also builds with MSVC.
std::int64_t checkedAdd(std::int64_t a, std::int64_t b) {
	if ((b > 0 && a > IntMax - b) || (b < 0 && a < IntMin - b)) overflow();
	return a + b;
}

std::int64_t checkedSubtract(std::int64_t a, std::int64_t b) {
	if ((b < 0 && a > IntMax + b) || (b > 0 && a < IntMin + b)) overflow();
	return a - b;
}

std::int64_t checkedMultiply(std::int64_t a, std::int64_t b) {
	if (a > 0) {
		if (b > 0 ? a > IntMax / b : b < IntMin / a) overflow();
	}
	else if (a < 0) {
		if (b > 0 ? a < IntMin / b : b < IntMax / a) overflow();
	}
	return a * b;
}

double checkedReal(double result) {
	if (!std::isfinite(result)) {
		throw RuntimeError("R005", "The result is too large for a REAL");
	}
	return result;
}

double asReal(const Value& value) {
	if (const auto* integer = std::get_if<std::int64_t>(&value)) {
		return static_cast<double>(*integer);
	}
	return std::get<double>(value);
}

std::string asText(const Value& value) {
	if (const auto* c = std::get_if<char>(&value)) {
		return std::string(1, *c);
	}
	return std::get<std::string>(value);
}

bool bothIntegers(const Value& left, const Value& right) {
	return std::holds_alternative<std::int64_t>(left) && std::holds_alternative<std::int64_t>(right);
}

// The type of an operand that must hold a usable value.
const Type* operandType(const Value& value) {
	if (isUnassigned(value)) {
		throw RuntimeError("R001", "A value is used before it was given one");
	}
	const Type* type = typeOf(value);
	if (!type) {
		throw RuntimeError("R008", "This object variable does not refer to an object yet. Create one with NEW");
	}
	return type;
}

void require(const OperatorCheck& check) {
	if (!check.ok()) {
		throw RuntimeError("R002", check.error);
	}
}

// Spec 5.4: an enumerated value moved `delta` places along its type.
EnumValue shiftEnum(const EnumValue& value, std::int64_t delta) {
	std::int64_t count = static_cast<std::int64_t>(value.type->enumValues.size());
	if (delta > -count && delta < count) {
		std::int64_t ordinal = value.ordinal + delta;
		if (ordinal >= 0 && ordinal < count) {
			return { value.type, static_cast<int>(ordinal) };
		}
	}
	const std::string& name = value.type->enumValues[static_cast<size_t>(value.ordinal)];
	std::string distance = delta == IntMin ? std::string("too many") : std::to_string(delta < 0 ? -delta : delta);
	bool forwards = delta > 0;
	throw RuntimeError("R009", "There is no " + value.type->name + " value " + distance
		+ (delta == 1 || delta == -1 ? " place " : " places ") + (forwards ? "after " : "before ") + name
		+ ". The " + (forwards ? "last" : "first") + " value is "
		+ (forwards ? value.type->enumValues.back() : value.type->enumValues.front()));
}

int sign(int compared) {
	return (compared > 0) - (compared < 0);
}

template <class T>
int compareOrdered(const T& a, const T& b) {
	return (b < a) - (a < b);
}

}

Value applyUnary(UnaryOp op, const Value& operand) {
	require(checkUnary(op, operandType(operand)));
	switch (op) {
	case UnaryOp::Not:
		return !std::get<bool>(operand);
	case UnaryOp::Plus:
		return operand;
	case UnaryOp::Negate:
		if (const auto* integer = std::get_if<std::int64_t>(&operand)) {
			if (*integer == IntMin) overflow();
			return -*integer;
		}
		return -std::get<double>(operand);
	}
	return operand;
}

Value applyBinary(BinaryOp op, const Value& left, const Value& right) {
	const Type* leftType = operandType(left);
	const Type* rightType = operandType(right);
	require(checkBinary(op, leftType, rightType));

	switch (op) {
	case BinaryOp::Or:           return std::get<bool>(left) || std::get<bool>(right);
	case BinaryOp::And:          return std::get<bool>(left) && std::get<bool>(right);
	case BinaryOp::Equal:        return valuesEqual(left, right);
	case BinaryOp::NotEqual:     return !valuesEqual(left, right);
	case BinaryOp::Less:         return compareValues(left, right) < 0;
	case BinaryOp::Greater:      return compareValues(left, right) > 0;
	case BinaryOp::LessEqual:    return compareValues(left, right) <= 0;
	case BinaryOp::GreaterEqual: return compareValues(left, right) >= 0;
	case BinaryOp::In:           return isInSet(left, *std::get<SetRef>(right));
	case BinaryOp::Concat:       return asText(left) + asText(right);

	case BinaryOp::Add:
	case BinaryOp::Subtract:
		if (leftType->kind == TypeKind::Enum) {
			std::int64_t delta = std::get<std::int64_t>(right);
			if (op == BinaryOp::Subtract) {
				delta = delta == IntMin ? IntMax : -delta;
			}
			return shiftEnum(std::get<EnumValue>(left), delta);
		}
		if (rightType->kind == TypeKind::Enum) {
			return shiftEnum(std::get<EnumValue>(right), std::get<std::int64_t>(left));
		}
		if (bothIntegers(left, right)) {
			std::int64_t a = std::get<std::int64_t>(left);
			std::int64_t b = std::get<std::int64_t>(right);
			return op == BinaryOp::Add ? checkedAdd(a, b) : checkedSubtract(a, b);
		}
		return checkedReal(op == BinaryOp::Add ? asReal(left) + asReal(right) : asReal(left) - asReal(right));

	case BinaryOp::Multiply:
		if (bothIntegers(left, right)) {
			return checkedMultiply(std::get<std::int64_t>(left), std::get<std::int64_t>(right));
		}
		return checkedReal(asReal(left) * asReal(right));

	case BinaryOp::Divide: {
		// Spec 6.2: / always gives a REAL.
		double divisor = asReal(right);
		if (divisor == 0.0) {
			throw RuntimeError("R004", "Division by zero");
		}
		return checkedReal(asReal(left) / divisor);
	}

	case BinaryOp::IntDiv:
	case BinaryOp::Mod: {
		// Truncated toward zero, so A = B * (A DIV B) + (A MOD B) (spec 6.2).
		std::int64_t a = std::get<std::int64_t>(left);
		std::int64_t b = std::get<std::int64_t>(right);
		if (b == 0) {
			throw RuntimeError("R004", std::string(op == BinaryOp::IntDiv ? "DIV" : "MOD") + " by zero");
		}
		if (a == IntMin && b == -1) {
			if (op == BinaryOp::Mod) return std::int64_t{ 0 };
			overflow();
		}
		return op == BinaryOp::IntDiv ? a / b : a % b;
	}
	}
	return {};
}

bool valuesEqual(const Value& left, const Value& right) {
	const Type* leftType = operandType(left);
	const Type* rightType = operandType(right);
	require(checkBinary(BinaryOp::Equal, leftType, rightType));

	if (leftType->isNumeric()) {
		if (bothIntegers(left, right)) {
			return std::get<std::int64_t>(left) == std::get<std::int64_t>(right);
		}
		return asReal(left) == asReal(right);
	}
	if (leftType->isText()) {
		return asText(left) == asText(right);
	}
	switch (leftType->kind) {
	case TypeKind::Boolean: return std::get<bool>(left) == std::get<bool>(right);
	case TypeKind::Date:    return std::get<Date>(left) == std::get<Date>(right);
	case TypeKind::Enum:    return std::get<EnumValue>(left).ordinal == std::get<EnumValue>(right).ordinal;
	case TypeKind::Pointer: return std::get<PointerValue>(left).sameTarget(std::get<PointerValue>(right));
	default:
		throw RuntimeError("R002", "Cannot compare " + describeType(leftType) + " with " + describeType(rightType));
	}
}

int compareValues(const Value& left, const Value& right) {
	const Type* leftType = operandType(left);
	const Type* rightType = operandType(right);
	require(checkBinary(BinaryOp::Less, leftType, rightType));

	if (leftType->isNumeric()) {
		if (bothIntegers(left, right)) {
			return compareOrdered(std::get<std::int64_t>(left), std::get<std::int64_t>(right));
		}
		return compareOrdered(asReal(left), asReal(right));
	}
	if (leftType->isText()) {
		// std::string compares bytes as unsigned char: by character code (spec 4.4).
		return sign(asText(left).compare(asText(right)));
	}
	switch (leftType->kind) {
	case TypeKind::Date: return compareOrdered(std::get<Date>(left), std::get<Date>(right));
	case TypeKind::Enum: return compareOrdered(std::get<EnumValue>(left).ordinal, std::get<EnumValue>(right).ordinal);
	default:
		throw RuntimeError("R002", "Cannot put " + describeType(leftType) + " and " + describeType(rightType) + " in order");
	}
}

bool isInSet(const Value& value, const SetValue& set) {
	for (const Value& element : set.elements) {
		if (valuesEqual(value, element)) {
			return true;
		}
	}
	return false;
}

bool requireBoolean(const Value& value, std::string_view what) {
	const Type* type = operandType(value);
	if (type->kind != TypeKind::Boolean) {
		throw RuntimeError("R002", std::string(what) + " must be TRUE or FALSE, but this is " + describeType(type));
	}
	return std::get<bool>(value);
}

std::int64_t requireInteger(const Value& value, std::string_view what) {
	const Type* type = operandType(value);
	if (type->kind != TypeKind::Integer) {
		std::string message = std::string(what) + " must be an INTEGER, but this is " + describeType(type);
		if (type->kind == TypeKind::Real) {
			message += ". Use INT() to convert it";
		}
		throw RuntimeError("R002", message);
	}
	return std::get<std::int64_t>(value);
}
