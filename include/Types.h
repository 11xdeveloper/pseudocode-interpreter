#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Ast.h"
#include "Diagnostic.h"

// The type system (spec 4, 5.3-5.7, 11).
//
// Every type is a `const Type*`. The six atomic types are static singletons;
// all other types are owned by a TypeTable and live as long as it does, so
// types may refer to each other freely, including in cycles such as a record
// holding a pointer to its own type.
//
// Records, enumerated types, sets and classes are nominal: each definition is
// a distinct type. Arrays and pointers are structural: ARRAY OF INTEGER and
// ^INTEGER are the same type wherever they are written.

enum class TypeKind {
	Integer, Real, Char, String, Boolean, Date,
	Array, Record, Enum, Pointer, Set, Class,
};

struct Type;

// A record field or class attribute.
struct Field {
	std::string name;
	const Type* type = nullptr;
	// Where the type was written. Array bounds are expressions evaluated when
	// a value is created, so creating a record or object needs this.
	const TypeSpec* spec = nullptr;
	Visibility visibility = Visibility::Public;
	const Type* owner = nullptr; // the record or class that declares it
	SourceLocation location;
};

struct Method {
	std::string name;
	const Subroutine* definition = nullptr;
	const Type* owner = nullptr; // the class that declares it
};

struct Type {
	TypeKind kind;
	std::string name;        // as written in its definition, or built, e.g. "ARRAY OF INTEGER"
	SourceLocation location; // of the definition; zero for built-in and structural types

	// Array: element type. Set: element type. Pointer: target type.
	const Type* element = nullptr;
	// Array: number of dimensions, or 0 for an "ARRAY OF T" parameter,
	// which accepts an array of any shape (spec 8.3).
	int dimensions = 0;

	// Enum: value names in declaration order; a value's ordinal is its index.
	std::vector<std::string> enumValues;

	// Record: fields. Class: attributes, inherited ones first, so an object's
	// attribute cells line up with this list.
	std::vector<Field> fields;

	// Class only.
	const Type* parent = nullptr;
	std::vector<Method> methods; // declared in this class; see findMethod
	const ClassDef* classDef = nullptr;

	bool isAtomic() const { return kind <= TypeKind::Date; }
	bool isNumeric() const { return kind == TypeKind::Integer || kind == TypeKind::Real; }
	bool isText() const { return kind == TypeKind::Char || kind == TypeKind::String; }
	bool isOpenArray() const { return kind == TypeKind::Array && dimensions == 0; }

	// Case-insensitive lookups (spec 2.2). Return -1 / nullptr when absent.
	int findField(std::string_view fieldName) const;
	int findEnumValue(std::string_view valueName) const;
	// The method this class uses for `methodName`: its own, else the nearest
	// ancestor's. Calling it on an object's actual class gives dynamic dispatch.
	const Method* findMethod(std::string_view methodName) const;
	// True if this class is `base` or inherits from it, directly or indirectly.
	bool isA(const Type* base) const;
};

// The atomic types.
const Type* integerType();
const Type* realType();
const Type* charType();
const Type* stringType();
const Type* booleanType();
const Type* dateType();
const Type* builtinType(BuiltinType type);

// "INTEGER", "Season", "ARRAY OF CHAR"; "unknown" for nullptr.
std::string typeName(const Type* type);
// For messages: "an INTEGER", "a STRING", "a Season value", "an array", "an object".
std::string describeType(const Type* type);

// Holds every user-defined and structural type of one program.
class TypeTable {
public:
	struct EnumConstant {
		const Type* type;
		int ordinal;
	};

	TypeTable() = default;
	TypeTable(const TypeTable&) = delete;
	TypeTable& operator=(const TypeTable&) = delete;

	// Collects the program's TYPE and CLASS definitions and resolves their
	// members. Problems are reported as semantic errors in diagnostics().
	void build(const Program& program);

	// A user-defined type or class by name, or nullptr.
	const Type* find(std::string_view name) const;
	// An enumerated value name such as Spring (spec 5.4).
	std::optional<EnumConstant> findEnumValue(std::string_view name) const;

	// Resolves a written type. An unknown name is reported (M001) and gives nullptr.
	const Type* resolve(const TypeSpec& spec);

	const Type* arrayOf(const Type* element, int dimensions);
	const Type* pointerTo(const Type* target);

	// User-defined types and classes in definition order.
	const std::vector<const Type*>& definedTypes() const { return defined_; }
	const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
	bool hasErrors() const;

	// Lists the defined types and their members, for --types.
	std::string dump() const;

private:
	Type* define(TypeKind kind, const Identifier& name, SourceLocation location);
	void completeClass(Type* classType, const ClassDef& def, std::map<const Type*, int>& state);
	void checkRecordCycles();
	void error(const char* code, SourceLocation location, std::string message);

	std::vector<std::unique_ptr<Type>> owned_;
	std::map<std::string, Type*> named_; // keyed by upper-case name
	std::map<std::string, EnumConstant> enumConstants_;
	std::map<std::pair<const Type*, int>, const Type*> arrays_;
	std::map<const Type*, const Type*> pointers_;
	std::vector<const Type*> defined_;
	std::vector<Diagnostic> diagnostics_;
};

// ---------- type rules ----------
//
// These answer "is this allowed, and what type results?" for known types.
// The semantic phase uses them on declared types; the runtime uses them on
// the types of actual values, so both phases apply identical rules and
// messages. A nullptr argument means "not known before running": the check
// then passes and the result is a best guess, or nullptr.

// Same type: identical, or structurally equal arrays or pointers.
bool sameType(const Type* a, const Type* b);

// Whether a value of type `source` may be stored where `target` is expected,
// after the implicit conversions INTEGER to REAL and CHAR to STRING (spec 4.3).
bool isAssignable(const Type* target, const Type* source);

// Why `source` cannot be stored where `target` is expected, or "" if it can.
std::string assignmentError(const Type* target, const Type* source);

struct OperatorCheck {
	const Type* result = nullptr; // nullptr if not known
	std::string error;            // empty if the operation is allowed

	bool ok() const { return error.empty(); }
};

OperatorCheck checkUnary(UnaryOp op, const Type* operand);
OperatorCheck checkBinary(BinaryOp op, const Type* left, const Type* right);

std::string_view operatorSymbol(BinaryOp op);
