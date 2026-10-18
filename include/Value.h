#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "Ast.h"
#include "Date.h"
#include "Types.h"

// Runtime values (spec 4.1, 4.2).
//
// A Value is a std::variant. std::monostate means "not yet given a value"
// (spec 5.1). Atomic values are held directly. Arrays, records, objects and
// sets are held through shared_ptr handles; copying a Value copies the
// handle, so code that needs value semantics (assignment, BYVAL) must call
// copyValue or storeValue.
//
// Every place a value can be stored is a Cell: a variable, an array element,
// a record field or an object attribute. Cells are shared, reference-counted
// objects, which gives three things the spec needs:
//   - BYREF parameters bind to the caller's cell (spec 8.3);
//   - pointers refer to a cell (spec 5.5), holding it weakly, so a pointer
//     to a local variable of a subroutine that has returned is detectably
//     dangling;
//   - aggregates are assigned element by element into existing cells, so
//     pointers and BYREF bindings to their elements stay valid.

struct Cell;
struct ArrayValue;
struct RecordValue;
struct ObjectValue;
struct SetValue;
using CellPtr = std::shared_ptr<Cell>;

struct EnumValue {
	const Type* type;
	int ordinal;
};

struct PointerValue {
	const Type* type;           // a Pointer type
	std::weak_ptr<Cell> target; // empty for a pointer never given an address

	bool isNull() const;
	// The same target cell (spec 4.4). Stays correct after the target is gone.
	bool sameTarget(const PointerValue& other) const;
};

using ArrayRef = std::shared_ptr<ArrayValue>;
using RecordRef = std::shared_ptr<RecordValue>;
using ObjectRef = std::shared_ptr<ObjectValue>; // a null handle is a null object (spec 11.3)
using SetRef = std::shared_ptr<const SetValue>; // sets are immutable (spec 4.2)

using Value = std::variant<
	std::monostate,
	std::int64_t, // INTEGER
	double,       // REAL
	char,         // CHAR
	std::string,  // STRING
	bool,         // BOOLEAN
	Date,         // DATE
	EnumValue, PointerValue, ArrayRef, RecordRef, ObjectRef, SetRef>;

struct Cell {
	// The declared type. For an undeclared variable it is null until the first
	// assignment, which fixes it (spec 5.1).
	const Type* type = nullptr;
	Value value;
};

struct ArrayBound {
	std::int64_t lower;
	std::int64_t upper;

	std::int64_t size() const { return upper - lower + 1; }
};

struct ArrayValue {
	const Type* type;
	std::vector<ArrayBound> bounds; // one per dimension
	std::vector<CellPtr> elements;  // row-major

	// The element at `indices`. Throws R002 for the wrong number of indices
	// and R003 for an index out of range; `name` is the array as written.
	const CellPtr& element(const std::vector<std::int64_t>& indices, std::string_view name) const;
	// "[1:30]" or "[1:3, 1:3]".
	std::string boundsText() const;
};

struct RecordValue {
	const Type* type;
	std::vector<CellPtr> fields; // matches type->fields
};

struct ObjectValue {
	const Type* type;                // the object's actual class
	std::vector<CellPtr> attributes; // matches type->fields
};

struct SetValue {
	const Type* type;
	std::vector<Value> elements; // distinct
};

// The type of a value: built-in for atomic values, carried by the value
// otherwise. nullptr for an unassigned value or a null object.
const Type* typeOf(const Value& value);

bool isUnassigned(const Value& value);

// A copy with value semantics: arrays and records are copied in full;
// objects and sets are shared (spec 4.2, 11.5).
Value copyValue(const Value& value);

// Applies the implicit conversions of spec 4.3 so the value can be stored
// where `target` is expected. Throws R002 if it cannot. An unassigned value
// passes through, so partly filled arrays can still be copied.
Value convertTo(const Type* target, Value value);

// Stores a value in a cell, as assignment does (spec 7.1): converted to the
// cell's type, and copied into an array or record element by element so the
// cells inside it are kept. An undeclared variable's cell takes the type of
// its first value. Throws R002 on a type or array-shape mismatch.
void storeValue(Cell& cell, const Value& value);

// Evaluates an array bound expression; supplied by the interpreter.
using BoundEvaluator = std::function<std::int64_t(const Expr&)>;

// The value a newly declared variable of `type` starts with. Arrays and
// records are built with a cell for every element or field; pointers and
// objects start as null. Atomic values start unassigned, or with zero values
// if `useDefaults` (the --defaults flag, spec 5.1).
//
// `spec` is the type as written: an array's bounds come from it, so it is
// needed whenever `type` is or contains an array. Throws R003 for invalid
// bounds.
Value makeValue(const Type* type, const TypeSpec* spec, const BoundEvaluator& evaluateBound, bool useDefaults);
CellPtr makeCell(const Type* type, const TypeSpec* spec, const BoundEvaluator& evaluateBound, bool useDefaults);

// A new object of `classType` with every attribute in its starting state.
// Attribute initialisers and the constructor are run by the interpreter.
ObjectRef makeObject(const Type* classType, const BoundEvaluator& evaluateBound, bool useDefaults);

// The cell a pointer refers to. Throws R008 if it is null or dangling.
CellPtr dereference(const PointerValue& pointer);

// Text in the OUTPUT format of spec 4.1, as used by OUTPUT, WRITEFILE and
// NUM_TO_STR. Throws R002 for values that cannot be output.
std::string formatValue(const Value& value);
// Shortest text that reads back as the same REAL, always with a decimal point.
std::string formatReal(double value);
// dd/mm/yyyy, zero-padded.
std::string formatDate(const Date& date);

// Whether text is a number: optional sign, digits, optionally '.' and digits.
bool isNumberText(std::string_view text);
// A number from text: INTEGER if it has no decimal point, else REAL. Empty
// if the text is not a number; throws R005 if an INTEGER is too large.
std::optional<Value> parseNumber(std::string_view text);
// A dd/mm/yyyy date from text, if valid.
std::optional<Date> parseDate(std::string_view text);

// Converts a line typed for INPUT to the target's type (spec 7.2). With a
// null target (an undeclared variable) the type is inferred from the text.
// Throws R002 if the text does not fit the type.
Value parseInput(std::string_view text, const Type* target);
