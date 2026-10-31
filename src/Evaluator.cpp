#include "Evaluator.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <istream>
#include <ostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten/stack.h>
#else
#include <pthread.h>
#endif

#include "Operations.h"

namespace {

// A runtime error's call chain is cut to this many calls at each end, so
// runaway recursion does not print thousands of lines.
constexpr size_t ChainEnds = 8;

// Each pseudocode call takes several C++ stack frames, so programs run on a
// thread with a large stack: enough for the default depth of 10000 calls
// (spec 8.4). The OS commits the memory only as it is used. Stack use is
// also checked on every call, so running out is R011 rather than a crash;
// the margin covers the expression and statement nesting within one call.
constexpr size_t StackSize = size_t{ 512 } << 20;
constexpr size_t StackMargin = size_t{ 16 } << 20;
// If no such thread can be made, the program runs on the calling thread,
// whose stack size is unknown, so a small limit is assumed.
constexpr size_t FallbackStackLimit = size_t{ 512 } << 10;

#ifdef _WIN32
DWORD WINAPI threadEntry(LPVOID body) {
	(*static_cast<std::function<void()>*>(body))();
	return 0;
}

bool runOnLargeStack(std::function<void()>& body) {
	HANDLE thread = CreateThread(nullptr, StackSize, threadEntry, &body, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
	if (!thread) {
		return false;
	}
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
	return true;
}
#elif defined(__EMSCRIPTEN__)
// WebAssembly has threads only where the page allows shared memory, so
// programs run on the calling thread. Its stack is set when linking
// (STACK_SIZE in CMakeLists.txt), so the limit is what is free of it.
bool runOnLargeStack(std::function<void()>&) {
	return false;
}
#else
void* threadEntry(void* body) {
	(*static_cast<std::function<void()>*>(body))();
	return nullptr;
}

bool runOnLargeStack(std::function<void()>& body) {
	pthread_attr_t attributes;
	if (pthread_attr_init(&attributes) != 0) {
		return false;
	}
	pthread_t thread;
	bool started = pthread_attr_setstacksize(&attributes, StackSize) == 0
		&& pthread_create(&thread, &attributes, threadEntry, &body) == 0;
	pthread_attr_destroy(&attributes);
	if (started) {
		pthread_join(thread, nullptr);
	}
	return started;
}
#endif

// Where the stack is now, to measure how much the program's calls use. The
// frame address is always on the real stack; the address of a local is not
// under AddressSanitizer, which can move locals to a separate "fake stack"
// to catch uses after return, making calls look far deeper than they are.
std::uintptr_t stackPosition() {
#if defined(__GNUC__) || defined(__clang__)
	return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#else
	char marker = 0;
	return reinterpret_cast<std::uintptr_t>(&marker);
#endif
}

Value literalValue(const Literal& literal) {
	return std::visit([](const auto& value) -> Value { return value; }, literal);
}

}

Evaluator::Evaluator(const Program& program, Environment& environment, std::istream& input, std::ostream& output,
	Options options)
	: program_(program), environment_(environment), types_(environment.types()),
	  input_(input), output_(output), options_(options),
	  files_(options.baseDirectory), builtins_(options.seed, files_) {
	evaluateBound_ = [this](const Expr& expr) { return integer(expr, "An array bound"); };
}

std::optional<Diagnostic> Evaluator::run() {
	std::optional<Diagnostic> result;
	std::exception_ptr failure;
	std::function<void()> body = [&] {
		try {
			result = runStatements();
		}
		catch (...) {
			failure = std::current_exception();
		}
	};
	stackLimit_ = StackSize - StackMargin;
	if (!runOnLargeStack(body)) {
#ifdef __EMSCRIPTEN__
		size_t free = emscripten_stack_get_free();
		stackLimit_ = free > 2 * StackMargin ? free - StackMargin : free / 2;
#else
		stackLimit_ = FallbackStackLimit;
#endif
		body();
	}
	output_.flush();
	if (failure) {
		std::rethrow_exception(failure);
	}
	return result;
}

std::optional<Diagnostic> Evaluator::runStatements() {
	stackBase_ = stackPosition();
	try {
		for (const StmtPtr& stmt : program_.statements) {
			execute(*stmt);
		}
	}
	catch (const LocatedError& error) {
		closeOpenFiles(true);
		return error.diagnostic;
	}
	closeOpenFiles(false);
	return std::nullopt;
}

// Spec 10.1: files still open when the program ends are closed with a
// warning. After a runtime error they are closed quietly, since the program
// never reached its CLOSEFILE, unless writing one back failed.
void Evaluator::closeOpenFiles(bool stoppedByError) {
	for (const Files::LeftOpen& file : files_.closeAll()) {
		if (stoppedByError && file.error.empty()) {
			continue;
		}
		Diagnostic warning;
		warning.severity = Severity::Warning;
		warning.phase = Phase::Runtime;
		warning.code = "W004";
		warning.line = file.openedAt.line;
		warning.column = file.openedAt.column;
		warning.message = "The file \"" + file.name + "\" was still open when the program ended, so it was closed "
			"automatically. Close it with CLOSEFILE";
		if (!file.error.empty()) {
			warning.message += ". " + file.error;
		}
		warnings_.push_back(std::move(warning));
	}
}

Evaluator::LocatedError Evaluator::located(const RuntimeError& error, SourceLocation location) const {
	Diagnostic diagnostic;
	diagnostic.severity = Severity::Error;
	diagnostic.phase = Phase::Runtime;
	diagnostic.code = error.code();
	diagnostic.line = location.line;
	diagnostic.column = location.column;
	diagnostic.message = error.what();

	std::vector<std::string> chain = environment_.callChain();
	if (chain.size() > 2 * ChainEnds) {
		size_t hidden = chain.size() - 2 * ChainEnds;
		chain.erase(chain.begin() + ChainEnds, chain.end() - ChainEnds);
		chain.insert(chain.begin() + ChainEnds, "... " + std::to_string(hidden) + " more calls ...");
	}
	diagnostic.notes = std::move(chain);
	return { std::move(diagnostic) };
}

const Type* Evaluator::resolveType(const TypeSpec& spec) {
	if (auto found = resolvedTypes_.find(&spec); found != resolvedTypes_.end()) {
		return found->second;
	}
	size_t errorsBefore = types_.diagnostics().size();
	const Type* type = types_.resolve(spec);
	if (!type) {
		std::string message = types_.diagnostics().size() > errorsBefore
			? types_.diagnostics().back().message : "Unknown type";
		throw RuntimeError("M001", message);
	}
	resolvedTypes_[&spec] = type;
	return type;
}

// ---------- statements ----------

Evaluator::Flow Evaluator::execute(const Stmt& stmt) {
	environment_.setCurrentLine(stmt.location.line);
	try {
		countStep();
		return executeNode(stmt);
	}
	catch (const RuntimeError& error) {
		throw located(error, stmt.location);
	}
}

// Spec 7.8: every statement counts towards --max-steps, and so does each
// time round a loop, so that a loop with an empty body still stops.
void Evaluator::countStep() {
	if (options_.maxSteps > 0 && ++steps_ > options_.maxSteps) {
		throw RuntimeError("R012", "The program was stopped after " + std::to_string(options_.maxSteps)
			+ " steps (--max-steps). It may be stuck in a loop that never ends");
	}
}

Evaluator::Flow Evaluator::executeBlock(const Block& block) {
	for (const StmtPtr& stmt : block) {
		if (execute(*stmt) == Flow::Return) {
			return Flow::Return;
		}
	}
	return Flow::Normal;
}

Evaluator::Flow Evaluator::executeNode(const Stmt& stmt) {
	return std::visit(Overloaded{
		[&](const DeclareStmt& node) {
			environment_.declare(node.name, resolveType(node.type), &node.type, evaluateBound_, &node);
			return Flow::Normal;
		},
		[&](const ConstantStmt& node) {
			environment_.defineConstant(node.name, literalValue(node.value), &node);
			return Flow::Normal;
		},
		[&](const AssignStmt& node) { executeAssign(node); return Flow::Normal; },
		[&](const InputStmt& node) { executeInput(node); return Flow::Normal; },
		[&](const OutputStmt& node) { executeOutput(node); return Flow::Normal; },
		[&](const CallStmt& node) { executeCall(node); return Flow::Normal; },
		[&](const IfStmt& node) {
			return condition(*node.condition, "The IF condition")
				? executeBlock(node.thenBranch) : executeBlock(node.elseBranch);
		},
		[&](const CaseStmt& node) { return executeCase(node); },
		[&](const ForStmt& node) { return executeFor(node); },
		[&](const RepeatStmt& node) {
			// Spec 7.6: the body runs at least once.
			do {
				countStep();
				if (executeBlock(node.body) == Flow::Return) return Flow::Return;
			} while (!condition(*node.condition, "The UNTIL condition"));
			return Flow::Normal;
		},
		[&](const WhileStmt& node) {
			while (condition(*node.condition, "The WHILE condition")) {
				countStep();
				if (executeBlock(node.body) == Flow::Return) return Flow::Return;
			}
			return Flow::Normal;
		},
		[&](const ReturnStmt& node) { return executeReturn(node); },
		[&](const DefineStmt& node) { executeDefine(node); return Flow::Normal; },
		[&](const auto&) { return executeFile(stmt); },
	}, stmt.node);
}

// Spec 7.1: the right side is evaluated in full before anything is stored.
void Evaluator::executeAssign(const AssignStmt& node) {
	Value value = evaluate(*node.value);
	Place target = requirePlace(*node.target, true);
	try {
		storeValue(*target.cell, value);
	}
	catch (const RuntimeError& error) {
		throw RuntimeError(error.code(), "Cannot assign to " + target.text + ". " + error.what());
	}
}

// Spec 7.2.
void Evaluator::executeInput(const InputStmt& node) {
	Place target = requirePlace(*node.target, true);
	output_.flush(); // so a prompt written by OUTPUT is visible first
	std::string line;
	if (!std::getline(input_, line)) {
		throw RuntimeError("R014", "End of input reached while INPUT was waiting for a value for " + target.text);
	}
	if (!line.empty() && line.back() == '\r') {
		line.pop_back();
	}
	try {
		storeValue(*target.cell, parseInput(line, target.cell->type));
	}
	catch (const RuntimeError& error) {
		throw RuntimeError(error.code(), "INPUT for " + target.text + ": " + error.what());
	}
}

// Spec 7.2: values are written with no separator, then a newline.
void Evaluator::executeOutput(const OutputStmt& node) {
	std::string line;
	for (const ExprPtr& expr : node.values) {
		Value value = evaluate(*expr);
		try {
			line += formatValue(value);
		}
		catch (const RuntimeError& error) {
			throw located(error, expr->location);
		}
	}
	output_ << line << '\n';
}

void Evaluator::executeCall(const CallStmt& node) {
	Callable callable = resolveCallee(*node.callee);
	if (callable.builtin) {
		throw RuntimeError("M005", callable.name + " is a built-in function, so it cannot be used as a statement. "
			"Use its result in an expression");
	}
	if (callable.subroutine && callable.subroutine->isFunction) {
		throw RuntimeError("M005", callable.name + " is a function, so it cannot be used as a statement. "
			"Use its result in an expression, as in Result ← " + callable.name + "(...)");
	}
	// Spec 8.1, 11.4: only methods may be called without CALL.
	if (!node.usesCallKeyword && !callable.target.self) {
		throw RuntimeError("M005", "To call the procedure " + callable.name + ", write CALL " + callable.name + "(...)");
	}
	invoke(callable, node.arguments);
}

// Spec 7.4.
Evaluator::Flow Evaluator::executeCase(const CaseStmt& node) {
	Value selector = evaluate(*node.selector);
	const Type* type = typeOf(selector);
	bool allowed = type && (type->kind == TypeKind::Integer || type->kind == TypeKind::Char
		|| type->kind == TypeKind::String || type->kind == TypeKind::Enum || type->kind == TypeKind::Date);
	if (!allowed) {
		throw located(RuntimeError("R002", "A CASE can only choose on an INTEGER, CHAR, STRING, DATE or enumerated value, but this is "
			+ describeType(type)), node.selector->location);
	}

	for (const CaseClause& clause : node.clauses) {
		for (const CaseLabel& label : clause.labels) {
			Value low = evaluate(*label.value);
			bool matches;
			try {
				if (label.upTo) {
					Value high = evaluate(*label.upTo);
					matches = compareValues(low, selector) <= 0 && compareValues(selector, high) <= 0;
				}
				else {
					matches = valuesEqual(selector, low);
				}
			}
			catch (const RuntimeError& error) {
				throw located(error, label.value->location);
			}
			if (matches) {
				return executeBlock(clause.body);
			}
		}
	}
	if (node.otherwise) {
		return executeBlock(*node.otherwise);
	}
	return Flow::Normal;
}

// Spec 7.5.
Evaluator::Flow Evaluator::executeFor(const ForStmt& node) {
	CellPtr cell = environment_.cellFor(node.variable, integerType());
	if (cell->type && cell->type->kind != TypeKind::Integer) {
		throw RuntimeError("R002", "The FOR loop variable " + node.variable.name + " must be an INTEGER, but it is "
			+ describeType(cell->type));
	}
	// Start, end and step are evaluated once, in that order.
	std::int64_t value = integer(*node.start, "The FOR start value");
	std::int64_t end = integer(*node.end, "The FOR end value");
	std::int64_t step = node.step ? integer(*node.step, "The STEP value") : 1;
	if (step == 0) {
		throw located(RuntimeError("R013", "The STEP of a FOR loop cannot be zero, or the loop would never end"),
			node.step->location);
	}

	storeValue(*cell, value);
	while (step > 0 ? value <= end : value >= end) {
		countStep();
		if (executeBlock(node.body) == Flow::Return) {
			return Flow::Return;
		}
		environment_.setCurrentLine(node.variable.location.line);
		// Checked, so running past the INTEGER range is R005, not wraparound.
		value = std::get<std::int64_t>(applyBinary(BinaryOp::Add, value, step));
		storeValue(*cell, value);
	}
	// The variable now holds the first value that failed the test.
	return Flow::Normal;
}

Evaluator::Flow Evaluator::executeReturn(const ReturnStmt& node) {
	const Subroutine* function = environment_.currentFrame().target.subroutine;
	if (!function) {
		throw RuntimeError("M008", "RETURN can only be used inside a FUNCTION");
	}
	if (!function->isFunction) {
		throw RuntimeError("M008", "A PROCEDURE does not use RETURN: it ends at ENDPROCEDURE");
	}
	Value value = evaluate(*node.value);
	returnValue_ = environment_.returnValue(*function, value);
	return Flow::Return;
}

// Spec 5.7: a named, read-only set.
void Evaluator::executeDefine(const DefineStmt& node) {
	const Type* setType = types_.find(node.setType.name);
	if (!setType) {
		throw RuntimeError("M001", "Unknown type " + node.setType.name);
	}
	if (setType->kind != TypeKind::Set) {
		throw RuntimeError("M004", node.setType.name + " is not a SET type, so DEFINE cannot use it");
	}
	auto set = std::make_shared<SetValue>();
	set->type = setType;
	for (const Literal& literal : node.values) {
		Value value = literalValue(literal);
		try {
			value = convertTo(setType->element, std::move(value));
		}
		catch (const RuntimeError&) {
			throw RuntimeError("R002", "The set " + node.name.name + " holds " + typeName(setType->element)
				+ " values, but " + formatValue(literalValue(literal)) + " is " + describeType(typeOf(literalValue(literal))));
		}
		bool duplicate = std::any_of(set->elements.begin(), set->elements.end(),
			[&](const Value& existing) { return valuesEqual(existing, value); });
		if (!duplicate) {
			set->elements.push_back(std::move(value));
		}
	}
	environment_.defineConstant(node.name, SetRef(std::move(set)), &node);
}

// ---------- files (spec 10) ----------

// Every file statement names its file first, so that is evaluated first.
Evaluator::Flow Evaluator::executeFile(const Stmt& stmt) {
	std::visit(Overloaded{
		[&](const OpenFileStmt& node) {
			files_.open(fileName(*node.file), node.mode, stmt.location);
		},
		[&](const CloseFileStmt& node) {
			files_.close(fileName(*node.file));
		},
		[&](const ReadFileStmt& node) {
			std::string name = fileName(*node.file);
			Place target = requirePlace(*node.target, true);
			if (target.cell->type && target.cell->type->kind != TypeKind::String) {
				throw RuntimeError("R002", "READFILE reads a line of text, so " + target.text + " must be a STRING, but it is "
					+ describeType(target.cell->type) + ". Read into a STRING, then convert it with STR_TO_NUM");
			}
			storeValue(*target.cell, files_.readLine(name));
		},
		[&](const WriteFileStmt& node) {
			std::string name = fileName(*node.file);
			Value value = evaluate(*node.value);
			const Type* type = typeOf(value);
			if (!type || !(type->isAtomic() || type->kind == TypeKind::Enum)) {
				std::string message = "WRITEFILE writes one value per line, so it cannot write " + describeType(type);
				if (type && type->kind == TypeKind::Array) message += ". Write its elements one at a time";
				if (type && type->kind == TypeKind::Record) message += ". Write its fields one at a time, or use a random file";
				throw located(RuntimeError("R002", message), node.value->location);
			}
			files_.writeLine(name, formatValue(value));
		},
		[&](const SeekStmt& node) {
			std::string name = fileName(*node.file);
			files_.seek(name, integer(*node.address, "The record address"));
		},
		[&](const GetRecordStmt& node) {
			std::string name = fileName(*node.file);
			Place target = requirePlace(*node.target, true);
			files_.getRecord(name, *target.cell, target.text);
		},
		[&](const PutRecordStmt& node) {
			std::string name = fileName(*node.file);
			std::optional<Place> source = place(*node.source, false);
			if (!source) {
				evaluate(*node.source); // reports an undeclared or unassigned name
				throw RuntimeError("R002", "PUTRECORD writes a variable, array element or field");
			}
			files_.putRecord(name, *source->cell, source->text);
		},
		[&](const auto&) {},
	}, stmt.node);
	return Flow::Normal;
}

// Spec 10: a file is named by a STRING, written directly or held in a variable.
std::string Evaluator::fileName(const Expr& expr) {
	Value value = evaluate(expr);
	if (const auto* text = std::get_if<std::string>(&value)) {
		return *text;
	}
	if (const auto* c = std::get_if<char>(&value)) {
		return std::string(1, *c);
	}
	throw located(RuntimeError("R002", "A file name must be a STRING, such as \"Data.txt\", but this is "
		+ describeType(typeOf(value))), expr.location);
}

// ---------- expressions ----------

Value Evaluator::evaluate(const Expr& expr) {
	try {
		return evaluateNode(expr);
	}
	catch (const RuntimeError& error) {
		throw located(error, expr.location);
	}
}

Value Evaluator::evaluateNode(const Expr& expr) {
	return std::visit(Overloaded{
		[&](const LiteralExpr& node) { return literalValue(node.value); },
		[&](const NameExpr& node) { return environment_.read(Identifier{ node.name, expr.location }); },
		[&](const UnaryExpr& node) { return applyUnary(node.op, evaluate(*node.operand)); },
		[&](const BinaryExpr& node) { return evaluateBinary(node); },
		[&](const IndexExpr&) { return readPlace(expr); },
		[&](const MemberExpr&) { return readPlace(expr); },
		[&](const DerefExpr&) { return readPlace(expr); },
		[&](const CallExpr& node) { return evaluateCall(node); },
		[&](const AddressOfExpr& node) { return evaluateAddressOf(node); },
		[&](const NewExpr& node) { return construct(node); },
		[&](const SuperExpr& node) -> Value {
			throw RuntimeError("M005", "SUPER." + node.member.name + " must be called, as in SUPER."
				+ node.member.name + "(...)");
		},
	}, expr.node);
}

Value Evaluator::evaluateBinary(const BinaryExpr& node) {
	Value left = evaluate(*node.left);
	if (node.op == BinaryOp::And || node.op == BinaryOp::Or) {
		// Spec 6.3: the right side is skipped if the left decides the result.
		if (const bool* known = std::get_if<bool>(&left)) {
			if (node.op == BinaryOp::And && !*known) return false;
			if (node.op == BinaryOp::Or && *known) return true;
		}
		else {
			// Report the left side's type error without evaluating the right.
			return applyBinary(node.op, left, true);
		}
	}
	Value right = evaluate(*node.right);
	return applyBinary(node.op, left, right);
}

bool Evaluator::condition(const Expr& expr, std::string_view what) {
	Value value = evaluate(expr);
	try {
		return requireBoolean(value, what);
	}
	catch (const RuntimeError& error) {
		throw located(error, expr.location);
	}
}

std::int64_t Evaluator::integer(const Expr& expr, std::string_view what) {
	Value value = evaluate(expr);
	try {
		return requireInteger(value, what);
	}
	catch (const RuntimeError& error) {
		throw located(error, expr.location);
	}
}

// Spec 5.5: ^ before a variable, element or field gives a pointer to its cell.
Value Evaluator::evaluateAddressOf(const AddressOfExpr& node) {
	std::optional<Place> target = place(*node.target, false);
	if (!target) {
		throw RuntimeError("R002", "^ can only take the address of a variable, array element or field");
	}
	if (target->isConstant) {
		throw RuntimeError("R002", target->text + " is a constant, so a pointer to it cannot be made");
	}
	if (!target->cell->type) {
		throw RuntimeError("R002", "Cannot take the address of " + target->text
			+ " before it has a type. Declare it, or give it a value first");
	}
	return PointerValue{ types_.pointerTo(target->cell->type), target->cell };
}

// ---------- places ----------

// The cell an expression names. For writing, a bare name that is not yet
// known becomes a new variable (spec 5.1). Returns nothing for expressions
// that are not storage, such as literals and calls.
std::optional<Evaluator::Place> Evaluator::place(const Expr& expr, bool forWrite) {
	return std::visit(Overloaded{
		[&](const NameExpr& node) -> std::optional<Place> {
			Identifier name{ node.name, expr.location };
			if (forWrite) {
				return Place{ environment_.cellFor(name), node.name };
			}
			std::optional<NameBinding> binding = environment_.lookup(node.name);
			if (binding && (binding->kind == NameBinding::Kind::Variable || binding->kind == NameBinding::Kind::Attribute)) {
				return Place{ binding->cell, binding->name, binding->isConstant };
			}
			return std::nullopt;
		},
		[&](const IndexExpr& node) -> std::optional<Place> {
			std::string text;
			Value container = containerOf(*node.array, text);
			std::vector<std::int64_t> indices;
			for (const ExprPtr& index : node.indices) {
				indices.push_back(integer(*index, "An array index"));
			}
			const auto* array = std::get_if<ArrayRef>(&container);
			if (!array || !*array) {
				throw RuntimeError("R002", text + " is not an array, so it cannot be indexed with [ ]");
			}
			try {
				CellPtr cell = (*array)->element(indices, text);
				std::string indexText;
				for (std::int64_t index : indices) {
					indexText += (indexText.empty() ? "" : ", ") + std::to_string(index);
				}
				return Place{ std::move(cell), text + "[" + indexText + "]" };
			}
			catch (const RuntimeError& error) {
				throw located(error, node.indices.front()->location);
			}
		},
		[&](const MemberExpr& node) -> std::optional<Place> {
			std::string text;
			Value container = containerOf(*node.object, text);
			if (const auto* object = std::get_if<ObjectRef>(&container)) {
				return Place{ environment_.attribute(*object, node.member), text + "." + node.member.name };
			}
			if (const auto* record = std::get_if<RecordRef>(&container); record && *record) {
				return Place{ environment_.field(*record, node.member), text + "." + node.member.name };
			}
			throw RuntimeError("R002", text + " is not a record or an object, so it has no " + node.member.name);
		},
		[&](const DerefExpr& node) -> std::optional<Place> {
			std::string text;
			Value container = containerOf(*node.pointer, text);
			const auto* pointer = std::get_if<PointerValue>(&container);
			if (!pointer) {
				throw RuntimeError("R002", text + " is not a pointer, so ^ cannot follow it");
			}
			return Place{ dereference(*pointer), text + "^" };
		},
		[&](const auto&) -> std::optional<Place> { return std::nullopt; },
	}, expr.node);
}

Evaluator::Place Evaluator::requirePlace(const Expr& expr, bool forWrite) {
	if (std::optional<Place> found = place(expr, forWrite)) {
		return std::move(*found);
	}
	throw RuntimeError("R002", "This is not a variable, array element or field, so it cannot be changed");
}

// The value an index, field or ^ is applied to, and how to name it.
Value Evaluator::containerOf(const Expr& expr, std::string& text) {
	if (std::optional<Place> found = place(expr, false)) {
		text = found->text;
		return found->cell->value;
	}
	text = "This value";
	return evaluate(expr);
}

Value Evaluator::readPlace(const Expr& expr) {
	Place found = requirePlace(expr, false);
	if (isUnassigned(found.cell->value)) {
		throw RuntimeError("R001", found.text + " used before it was given a value");
	}
	return found.cell->value;
}

// ---------- calls ----------

Value Evaluator::evaluateCall(const CallExpr& node) {
	Callable callable = resolveCallee(*node.callee);
	if (callable.builtin) {
		return callBuiltin(callable, node.arguments);
	}
	if (!callable.subroutine || !callable.subroutine->isFunction) {
		throw RuntimeError("M005", callable.name + " is a procedure and does not return a value; use CALL");
	}
	return invoke(callable, node.arguments);
}

Evaluator::Callable Evaluator::resolveCallee(const Expr& callee) {
	if (const auto* name = std::get_if<NameExpr>(&callee.node)) {
		std::optional<NameBinding> binding = environment_.lookup(name->name);
		if (!binding) {
			throw RuntimeError("M001", "There is no procedure or function called " + name->name);
		}
		switch (binding->kind) {
		case NameBinding::Kind::Subroutine:
			return { binding->subroutine, { binding->subroutine, nullptr, nullptr }, binding->name };
		case NameBinding::Kind::Method: {
			// A bare method name inside a method calls it on the same object.
			// Public methods dispatch on the object's actual class (spec 11.4).
			const ObjectRef& self = environment_.currentFrame().target.self;
			const Method* method = binding->method;
			if (method->definition->visibility == Visibility::Public) {
				method = self->type->findMethod(name->name);
			}
			return { method->definition, { method->definition, self, method->owner }, method->name };
		}
		case NameBinding::Kind::Builtin:
			return { nullptr, {}, binding->name, true };
		case NameBinding::Kind::Variable:
		case NameBinding::Kind::Attribute:
			if (std::holds_alternative<ArrayRef>(binding->cell->value)) {
				throw RuntimeError("M005", binding->name + " is an array. Use square brackets for an index, as in "
					+ binding->name + "[1]");
			}
			[[fallthrough]];
		default:
			throw RuntimeError("M005", binding->name + " is not a procedure or function, so it cannot be called");
		}
	}

	if (const auto* member = std::get_if<MemberExpr>(&callee.node)) {
		std::string text;
		Value container = containerOf(*member->object, text);
		if (const auto* object = std::get_if<ObjectRef>(&container)) {
			const Method& method = environment_.method(*object, member->member);
			return { method.definition, { method.definition, *object, method.owner }, method.name };
		}
		throw RuntimeError("R002", text + " is not an object, so it has no methods");
	}

	if (const auto* super = std::get_if<SuperExpr>(&callee.node)) {
		const Method* method = environment_.superMethod(super->member);
		const ObjectRef& self = environment_.currentFrame().target.self;
		if (!method) {
			return { nullptr, { nullptr, self, nullptr }, "NEW" }; // the parent's implicit constructor
		}
		return { method->definition, { method->definition, self, method->owner }, method->name };
	}

	throw RuntimeError("R002", "This value cannot be called");
}

// Spec 8.3: arguments are evaluated left to right, then bound.
Value Evaluator::invoke(const Callable& callable, const std::vector<ExprPtr>& arguments) {
	if (!callable.subroutine) {
		// An implicit constructor takes no arguments and does nothing (spec 11.3).
		if (!arguments.empty()) {
			throw RuntimeError("M003", "The parent class has no constructor (PROCEDURE NEW), so SUPER.NEW takes no arguments");
		}
		return {};
	}
	const Subroutine& subroutine = *callable.subroutine;

	std::uintptr_t position = stackPosition();
	if ((stackBase_ > position ? stackBase_ - position : position - stackBase_) > stackLimit_) {
		throw RuntimeError("R011", "Too many nested calls (possible infinite recursion) in " + subroutine.name.name);
	}

	std::vector<Argument> bound(arguments.size());
	for (size_t i = 0; i < arguments.size(); ++i) {
		bool passByReference = i < subroutine.parameters.size()
			&& subroutine.parameters[i].mode == PassMode::ByReference;
		if (passByReference) {
			bound[i] = byReference(*arguments[i]);
		}
		else {
			bound[i].value = evaluate(*arguments[i]);
		}
	}

	environment_.enterCall(callable.target, std::move(bound), evaluateBound_);
	CallScope scope(environment_);
	Flow flow = executeBlock(subroutine.body);
	if (!subroutine.isFunction) {
		return {};
	}
	if (flow != Flow::Return) {
		throw located(RuntimeError("R007", "Function " + subroutine.name.name + " ended without returning a value"),
			subroutine.location);
	}
	Value result = std::move(returnValue_);
	returnValue_ = {};
	return result;
}

// A BYREF argument is the caller's cell, if it has one (spec 8.3).
Argument Evaluator::byReference(const Expr& argument) {
	Argument result;
	if (const auto* name = std::get_if<NameExpr>(&argument.node)) {
		std::optional<NameBinding> binding = environment_.lookup(name->name);
		if (!binding) {
			// An undeclared variable: created here, typed by the parameter.
			result.cell = environment_.cellFor(Identifier{ name->name, argument.location });
		}
		else if (binding->kind == NameBinding::Kind::Variable || binding->kind == NameBinding::Kind::Attribute) {
			result.cell = binding->cell;
			result.isConstant = binding->isConstant;
		}
		return result;
	}
	try {
		if (std::optional<Place> found = place(argument, false)) {
			result.cell = found->cell;
		}
	}
	catch (const RuntimeError& error) {
		throw located(error, argument.location);
	}
	return result;
}

// Spec 11.3: create the object, run every class's attribute initialisers,
// parent first, then the class's own constructor.
Value Evaluator::construct(const NewExpr& node) {
	const Type* classType = types_.find(node.className.name);
	if (!classType) {
		throw RuntimeError("M001", "There is no class called " + node.className.name);
	}
	if (classType->kind != TypeKind::Class) {
		throw RuntimeError("M004", node.className.name + " is not a class, so NEW cannot create one");
	}
	ObjectRef object = makeObject(classType, evaluateBound_, environment_.options().useDefaults);

	std::vector<const Type*> chain;
	for (const Type* type = classType; type; type = type->parent) {
		chain.push_back(type);
	}
	std::reverse(chain.begin(), chain.end());
	for (const Type* type : chain) {
		if (!type->classDef || type->classDef->initialisers.empty()) {
			continue;
		}
		environment_.enterCall({ nullptr, object, type }, {}, evaluateBound_);
		CallScope scope(environment_);
		executeBlock(type->classDef->initialisers);
	}

	const Method* constructor = Environment::constructorOf(classType);
	if (!constructor) {
		if (!node.arguments.empty()) {
			throw RuntimeError("M003", classType->name + " has no constructor (PROCEDURE NEW), so NEW "
				+ classType->name + "() takes no arguments");
		}
		return object;
	}
	invoke({ constructor->definition, { constructor->definition, object, classType }, classType->name + ".NEW" },
		node.arguments);
	return object;
}

// Spec 9: all arguments are passed by value, evaluated left to right.
Value Evaluator::callBuiltin(const Callable& callable, const std::vector<ExprPtr>& arguments) {
	std::vector<Value> values;
	values.reserve(arguments.size());
	for (const ExprPtr& argument : arguments) {
		values.push_back(evaluate(*argument));
	}
	try {
		return builtins_.call(callable.name, std::move(values));
	}
	catch (const ArgumentError& error) {
		throw located(error, arguments[error.index()]->location);
	}
}
