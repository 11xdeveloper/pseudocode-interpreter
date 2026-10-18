#pragma once

#include <cstdint>
#include <string_view>

#include "Ast.h"
#include "Value.h"

// Operators on runtime values (spec 4.4, 6).
//
// Each operation first checks its operands' types with checkUnary or
// checkBinary from Types.h, the same rules the semantic phase applies to
// declared types, and throws R002 with the same message if they fail. Then:
//   - INTEGER overflow is R005, never wraparound (spec 4.1);
//   - division, DIV or MOD by zero is R004 (spec 6.2);
//   - moving an enumerated value past either end is R009 (spec 5.4);
//   - using an unassigned value is R001, and a null object R008.

Value applyUnary(UnaryOp op, const Value& operand);

// AND and OR here take both values. The interpreter short-circuits them
// (spec 6.3) by testing the left value before evaluating the right one.
Value applyBinary(BinaryOp op, const Value& left, const Value& right);

// The = of spec 4.4. Throws R002 for values that cannot be compared.
bool valuesEqual(const Value& left, const Value& right);

// Negative, zero or positive as left is before, equal to or after right, for
// types that have an order (spec 4.4). Throws R002 otherwise.
int compareValues(const Value& left, const Value& right);

// The IN operator (spec 5.7).
bool isInSet(const Value& value, const SetValue& set);

// For conditions and FOR bounds. `what` names the value in the error, e.g.
// "The IF condition" or "The STEP value".
bool requireBoolean(const Value& value, std::string_view what);
std::int64_t requireInteger(const Value& value, std::string_view what);
