#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Date.h"

// Abstract syntax tree built by the Parser (spec section 3).
//
// Expressions, statements and types are a source location plus a std::variant
// over their node kinds, so later phases walk them with std::visit. Children
// are owned through unique_ptr. Names keep the spelling written in the source;
// resolving them case-insensitively (spec 2.2) is left to later phases.
//
// A program that produced syntax errors may contain null ExprPtr children
// where a part could not be parsed. Such a tree must not be executed.

struct SourceLocation {
	int line = 0;   // 1-based
	int column = 0; // 1-based
};

// The value of a literal. Unlike LiteralValue in Token.h this includes
// BOOLEAN, since TRUE and FALSE are literals here rather than keywords.
using Literal = std::variant<bool, std::int64_t, double, char, std::string, Date>;

struct Expr;
struct Stmt;
struct TypeSpec;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using TypeSpecPtr = std::unique_ptr<TypeSpec>;
using Block = std::vector<StmtPtr>;

// A name together with where it was written.
struct Identifier {
	std::string name;
	SourceLocation location;
};

// Lets std::visit take a set of lambdas: std::visit(Overloaded{ ... }, node).
template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};

// ---------- types (spec 3.1 type_spec, param_type) ----------

enum class BuiltinType { Integer, Real, Char, String, Boolean, Date };

// A user-defined type, record, enumerated, pointer, set or class, by name.
struct NamedType {
	std::string name;
};

struct ArrayBounds {
	ExprPtr lower;
	ExprPtr upper;
};

struct ArrayType {
	// One entry per dimension. Empty for an "ARRAY OF T" parameter, which
	// takes its bounds from the argument (spec 8.3).
	std::vector<ArrayBounds> bounds;
	TypeSpecPtr element;
};

struct TypeSpec {
	SourceLocation location;
	std::variant<BuiltinType, NamedType, ArrayType> node;
};

// ---------- expressions (spec 3.1, 6) ----------

enum class UnaryOp { Negate, Plus, Not };

enum class BinaryOp {
	Or, And,
	Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual, In,
	Concat,
	Add, Subtract,
	Multiply, Divide, IntDiv, Mod,
};

struct LiteralExpr {
	Literal value;
};

// A variable, constant, enumerated value, subroutine or built-in function name.
// MOD( and DIV( at the start of an operand are names too (spec 9.2).
struct NameExpr {
	std::string name;
};

struct UnaryExpr {
	UnaryOp op;
	ExprPtr operand;
};

struct BinaryExpr {
	BinaryOp op;
	ExprPtr left;
	ExprPtr right;
};

// Array[i] or Grid[i, j].
struct IndexExpr {
	ExprPtr array;
	std::vector<ExprPtr> indices;
};

// A function or method call. The callee is a NameExpr, a MemberExpr for
// Object.Method(...), or a SuperExpr for SUPER.Method(...).
struct CallExpr {
	ExprPtr callee;
	std::vector<ExprPtr> arguments;
};

// Record field or object member: Object.Member.
struct MemberExpr {
	ExprPtr object;
	Identifier member;
};

// Prefix ^: the address of a variable, array element or field (spec 5.5).
struct AddressOfExpr {
	ExprPtr target;
};

// Postfix ^: the value a pointer refers to.
struct DerefExpr {
	ExprPtr pointer;
};

// NEW ClassName(arguments).
struct NewExpr {
	Identifier className;
	std::vector<ExprPtr> arguments;
};

// SUPER.Member, the parent class's version of a method (spec 11.4).
struct SuperExpr {
	Identifier member;
};

struct Expr {
	// For operators this is the operator token; otherwise the first token.
	SourceLocation location;
	std::variant<
		LiteralExpr, NameExpr, UnaryExpr, BinaryExpr, IndexExpr, CallExpr,
		MemberExpr, AddressOfExpr, DerefExpr, NewExpr, SuperExpr> node;
};

// ---------- statements (spec 3.1, 7, 10) ----------

struct DeclareStmt {
	Identifier name;
	TypeSpec type;
};

struct ConstantStmt {
	Identifier name;
	Literal value; // a leading minus is already applied
};

// target is a NameExpr, or an IndexExpr, MemberExpr or DerefExpr built on one.
struct AssignStmt {
	ExprPtr target;
	ExprPtr value;
};

struct InputStmt {
	ExprPtr target;
};

struct OutputStmt {
	std::vector<ExprPtr> values;
};

// A procedure or method call used as a statement.
struct CallStmt {
	ExprPtr callee; // as for CallExpr
	std::vector<ExprPtr> arguments;
	bool usesCallKeyword = true; // false for a bare method call such as Pet.Speak()
};

struct IfStmt {
	ExprPtr condition;
	Block thenBranch;
	Block elseBranch;
};

struct CaseLabel {
	ExprPtr value; // LiteralExpr, or NameExpr for a constant or enumerated value
	ExprPtr upTo;  // the Y of "X TO Y"; null for a single value
};

struct CaseClause {
	SourceLocation location;
	std::vector<CaseLabel> labels;
	Block body;
};

struct CaseStmt {
	ExprPtr selector;
	std::vector<CaseClause> clauses;
	std::optional<Block> otherwise;
};

struct ForStmt {
	Identifier variable;
	ExprPtr start;
	ExprPtr end;
	ExprPtr step; // null when there is no STEP
	Block body;
};

struct RepeatStmt {
	Block body;
	ExprPtr condition;
};

struct WhileStmt {
	ExprPtr condition;
	Block body;
};

struct ReturnStmt {
	ExprPtr value;
};

enum class FileMode { Read, Write, Append, Random };

struct OpenFileStmt {
	ExprPtr file;
	FileMode mode;
};

struct ReadFileStmt {
	ExprPtr file;
	ExprPtr target;
};

struct WriteFileStmt {
	ExprPtr file;
	ExprPtr value;
};

struct CloseFileStmt {
	ExprPtr file;
};

struct SeekStmt {
	ExprPtr file;
	ExprPtr address;
};

struct GetRecordStmt {
	ExprPtr file;
	ExprPtr target;
};

struct PutRecordStmt {
	ExprPtr file;
	ExprPtr source;
};

// DEFINE Name (values) : SetType (spec 5.7). It runs in order with the other
// top-level statements rather than being hoisted.
struct DefineStmt {
	Identifier name;
	std::vector<Literal> values;
	Identifier setType;
};

struct Stmt {
	SourceLocation location; // the statement's first token
	std::variant<
		DeclareStmt, ConstantStmt, AssignStmt, InputStmt, OutputStmt, CallStmt,
		IfStmt, CaseStmt, ForStmt, RepeatStmt, WhileStmt, ReturnStmt,
		OpenFileStmt, ReadFileStmt, WriteFileStmt, CloseFileStmt, SeekStmt,
		GetRecordStmt, PutRecordStmt, DefineStmt> node;
};

// ---------- definitions (spec 3.1, 5.4-5.7, 8, 11) ----------

enum class PassMode { ByValue, ByReference };

enum class Visibility { Public, Private };

struct Parameter {
	Identifier name;
	// Already resolved: a BYVAL or BYREF keyword carries on to the following
	// parameters until the other keyword appears (spec 8.3).
	PassMode mode = PassMode::ByValue;
	TypeSpec type;
};

// A procedure or function, at top level or as a class method.
struct Subroutine {
	SourceLocation location;
	bool isFunction = false;
	Identifier name; // "NEW" for a constructor
	std::vector<Parameter> parameters;
	std::optional<TypeSpec> returnType; // functions only
	Block body;
	Visibility visibility = Visibility::Public; // methods only
};

struct EnumTypeDef {
	std::vector<Identifier> values;
};

struct PointerTypeDef {
	TypeSpec target;
};

struct SetTypeDef {
	TypeSpec element;
};

struct RecordTypeDef {
	std::vector<DeclareStmt> fields;
};

struct TypeDef {
	SourceLocation location;
	Identifier name;
	std::variant<EnumTypeDef, PointerTypeDef, SetTypeDef, RecordTypeDef> body;
};

struct Attribute {
	Identifier name;
	TypeSpec type;
	Visibility visibility = Visibility::Public;
};

struct ClassDef {
	SourceLocation location;
	Identifier name;
	std::optional<Identifier> parent;
	std::vector<Attribute> attributes;
	std::vector<Subroutine> methods;
	// Assignments written directly in the class body, run in order when an
	// object is created (spec 11.1). Each is an AssignStmt.
	Block initialisers;
};

// Definitions are hoisted (spec 3), so they are kept apart from the top-level
// statements, which run in order.
struct Program {
	std::vector<TypeDef> types;
	std::vector<Subroutine> subroutines;
	std::vector<ClassDef> classes;
	Block statements;
};
