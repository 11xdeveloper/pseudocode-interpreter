#include "AstPrinter.h"

#include <charconv>
#include <cstdio>

namespace {

std::string_view binaryOpName(BinaryOp op) {
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

std::string_view unaryOpName(UnaryOp op) {
	switch (op) {
	case UnaryOp::Negate: return "-";
	case UnaryOp::Plus:   return "+";
	case UnaryOp::Not:    return "NOT";
	}
	return "?";
}

std::string_view builtinTypeName(BuiltinType type) {
	switch (type) {
	case BuiltinType::Integer: return "INTEGER";
	case BuiltinType::Real:    return "REAL";
	case BuiltinType::Char:    return "CHAR";
	case BuiltinType::String:  return "STRING";
	case BuiltinType::Boolean: return "BOOLEAN";
	case BuiltinType::Date:    return "DATE";
	}
	return "?";
}

std::string_view fileModeName(FileMode mode) {
	switch (mode) {
	case FileMode::Read:   return "READ";
	case FileMode::Write:  return "WRITE";
	case FileMode::Append: return "APPEND";
	case FileMode::Random: return "RANDOM";
	}
	return "?";
}

std::string printType(const TypeSpec& type) {
	return std::visit(Overloaded{
		[](BuiltinType builtin) { return std::string(builtinTypeName(builtin)); },
		[](const NamedType& named) { return named.name; },
		[](const ArrayType& array) {
			std::string out = "ARRAY";
			if (!array.bounds.empty()) {
				out += '[';
				for (size_t i = 0; i < array.bounds.size(); ++i) {
					if (i > 0) out += ", ";
					out += printExpr(array.bounds[i].lower.get()) + ':' + printExpr(array.bounds[i].upper.get());
				}
				out += ']';
			}
			out += " OF ";
			out += array.element ? printType(*array.element) : "<error>";
			return out;
		},
	}, type.node);
}

std::string printList(const std::vector<ExprPtr>& exprs) {
	std::string out;
	for (const ExprPtr& expr : exprs) {
		out += ' ' + printExpr(expr.get());
	}
	return out;
}

class Printer {
public:
	std::string out;

	void line(int depth, const std::string& text) {
		out.append(static_cast<size_t>(depth) * 2, ' ');
		out += text;
		out += '\n';
	}

	void block(int depth, const Block& statements) {
		for (const StmtPtr& stmt : statements) {
			statement(depth, *stmt);
		}
	}

	void statement(int depth, const Stmt& stmt) {
		std::visit(Overloaded{
			[&](const DeclareStmt& node) {
				line(depth, "Declare " + node.name.name + " : " + printType(node.type));
			},
			[&](const ConstantStmt& node) {
				line(depth, "Constant " + node.name.name + " = " + printLiteral(node.value));
			},
			[&](const AssignStmt& node) {
				line(depth, "Assign " + printExpr(node.target.get()) + " ← " + printExpr(node.value.get()));
			},
			[&](const InputStmt& node) {
				line(depth, "Input " + printExpr(node.target.get()));
			},
			[&](const OutputStmt& node) {
				line(depth, "Output" + printList(node.values));
			},
			[&](const CallStmt& node) {
				line(depth, std::string(node.usesCallKeyword ? "Call " : "MethodCall ")
					+ printExpr(node.callee.get()) + " (" + printList(node.arguments) + " )");
			},
			[&](const IfStmt& node) {
				line(depth, "If " + printExpr(node.condition.get()));
				line(depth + 1, "Then");
				block(depth + 2, node.thenBranch);
				if (!node.elseBranch.empty()) {
					line(depth + 1, "Else");
					block(depth + 2, node.elseBranch);
				}
			},
			[&](const CaseStmt& node) {
				line(depth, "Case " + printExpr(node.selector.get()));
				for (const CaseClause& clause : node.clauses) {
					std::string labels;
					for (const CaseLabel& label : clause.labels) {
						if (!labels.empty()) labels += ", ";
						labels += printExpr(label.value.get());
						if (label.upTo) labels += " TO " + printExpr(label.upTo.get());
					}
					line(depth + 1, "When " + labels);
					block(depth + 2, clause.body);
				}
				if (node.otherwise) {
					line(depth + 1, "Otherwise");
					block(depth + 2, *node.otherwise);
				}
			},
			[&](const ForStmt& node) {
				std::string header = "For " + node.variable.name + " ← " + printExpr(node.start.get())
					+ " TO " + printExpr(node.end.get());
				if (node.step) header += " STEP " + printExpr(node.step.get());
				line(depth, header);
				block(depth + 1, node.body);
			},
			[&](const RepeatStmt& node) {
				line(depth, "Repeat");
				block(depth + 1, node.body);
				line(depth, "Until " + printExpr(node.condition.get()));
			},
			[&](const WhileStmt& node) {
				line(depth, "While " + printExpr(node.condition.get()));
				block(depth + 1, node.body);
			},
			[&](const ReturnStmt& node) {
				line(depth, "Return " + printExpr(node.value.get()));
			},
			[&](const OpenFileStmt& node) {
				line(depth, "OpenFile " + printExpr(node.file.get()) + " FOR " + std::string(fileModeName(node.mode)));
			},
			[&](const ReadFileStmt& node) {
				line(depth, "ReadFile " + printExpr(node.file.get()) + ", " + printExpr(node.target.get()));
			},
			[&](const WriteFileStmt& node) {
				line(depth, "WriteFile " + printExpr(node.file.get()) + ", " + printExpr(node.value.get()));
			},
			[&](const CloseFileStmt& node) {
				line(depth, "CloseFile " + printExpr(node.file.get()));
			},
			[&](const SeekStmt& node) {
				line(depth, "Seek " + printExpr(node.file.get()) + ", " + printExpr(node.address.get()));
			},
			[&](const GetRecordStmt& node) {
				line(depth, "GetRecord " + printExpr(node.file.get()) + ", " + printExpr(node.target.get()));
			},
			[&](const PutRecordStmt& node) {
				line(depth, "PutRecord " + printExpr(node.file.get()) + ", " + printExpr(node.source.get()));
			},
			[&](const DefineStmt& node) {
				std::string values;
				for (const Literal& value : node.values) {
					if (!values.empty()) values += ", ";
					values += printLiteral(value);
				}
				line(depth, "Define " + node.name.name + " (" + values + ") : " + node.setType.name);
			},
		}, stmt.node);
	}

	void subroutine(int depth, const Subroutine& sub, bool isMethod) {
		std::string header;
		if (isMethod) {
			header += sub.visibility == Visibility::Private ? "PRIVATE " : "PUBLIC ";
		}
		header += sub.isFunction ? "Function " : "Procedure ";
		header += sub.name.name + '(';
		for (size_t i = 0; i < sub.parameters.size(); ++i) {
			const Parameter& parameter = sub.parameters[i];
			if (i > 0) header += ", ";
			header += parameter.mode == PassMode::ByReference ? "BYREF " : "BYVAL ";
			header += parameter.name.name + " : " + printType(parameter.type);
		}
		header += ')';
		if (sub.returnType) {
			header += " RETURNS " + printType(*sub.returnType);
		}
		line(depth, header);
		block(depth + 1, sub.body);
	}

	void typeDef(int depth, const TypeDef& def) {
		std::visit(Overloaded{
			[&](const EnumTypeDef& node) {
				std::string values;
				for (const Identifier& value : node.values) {
					if (!values.empty()) values += ", ";
					values += value.name;
				}
				line(depth, "Type " + def.name.name + " = (" + values + ")");
			},
			[&](const PointerTypeDef& node) {
				line(depth, "Type " + def.name.name + " = ^" + printType(node.target));
			},
			[&](const SetTypeDef& node) {
				line(depth, "Type " + def.name.name + " = SET OF " + printType(node.element));
			},
			[&](const RecordTypeDef& node) {
				line(depth, "Type " + def.name.name);
				for (const DeclareStmt& field : node.fields) {
					line(depth + 1, "Field " + field.name.name + " : " + printType(field.type));
				}
			},
		}, def.body);
	}

	void classDef(int depth, const ClassDef& def) {
		std::string header = "Class " + def.name.name;
		if (def.parent) {
			header += " INHERITS " + def.parent->name;
		}
		line(depth, header);
		for (const Attribute& attribute : def.attributes) {
			line(depth + 1, std::string("Attribute ") + (attribute.visibility == Visibility::Private ? "PRIVATE " : "PUBLIC ")
				+ attribute.name.name + " : " + printType(attribute.type));
		}
		for (const StmtPtr& initialiser : def.initialisers) {
			statement(depth + 1, *initialiser);
		}
		for (const Subroutine& method : def.methods) {
			subroutine(depth + 1, method, true);
		}
	}
};

}

std::string printLiteral(const Literal& literal) {
	return std::visit(Overloaded{
		[](bool value) { return std::string(value ? "TRUE" : "FALSE"); },
		[](std::int64_t value) { return std::to_string(value); },
		[](double value) {
			char buffer[64];
			auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
			std::string text(buffer, result.ptr);
			if (text.find_first_of(".eEni") == std::string::npos) {
				text += ".0";
			}
			return text;
		},
		[](char value) { return std::string("'") + value + "'"; },
		[](const std::string& value) { return "\"" + value + "\""; },
		[](const Date& value) {
			char buffer[16];
			std::snprintf(buffer, sizeof buffer, "%02d/%02d/%04d", value.day, value.month, value.year);
			return std::string(buffer);
		},
	}, literal);
}

std::string printExpr(const Expr* expr) {
	if (!expr) {
		return "<error>";
	}
	return std::visit(Overloaded{
		[](const LiteralExpr& node) { return printLiteral(node.value); },
		[](const NameExpr& node) { return node.name; },
		[](const UnaryExpr& node) {
			return "(" + std::string(unaryOpName(node.op)) + ' ' + printExpr(node.operand.get()) + ')';
		},
		[](const BinaryExpr& node) {
			return "(" + std::string(binaryOpName(node.op)) + ' ' + printExpr(node.left.get()) + ' '
				+ printExpr(node.right.get()) + ')';
		},
		[](const IndexExpr& node) {
			return "(index " + printExpr(node.array.get()) + printList(node.indices) + ')';
		},
		[](const CallExpr& node) {
			return "(call " + printExpr(node.callee.get()) + printList(node.arguments) + ')';
		},
		[](const MemberExpr& node) {
			return "(. " + printExpr(node.object.get()) + ' ' + node.member.name + ')';
		},
		[](const AddressOfExpr& node) {
			return "(address-of " + printExpr(node.target.get()) + ')';
		},
		[](const DerefExpr& node) {
			return "(deref " + printExpr(node.pointer.get()) + ')';
		},
		[](const NewExpr& node) {
			return "(new " + node.className.name + printList(node.arguments) + ')';
		},
		[](const SuperExpr& node) {
			return "SUPER." + node.member.name;
		},
	}, expr->node);
}

std::string printAst(const Program& program) {
	Printer printer;
	for (const TypeDef& def : program.types) {
		printer.typeDef(0, def);
	}
	for (const ClassDef& def : program.classes) {
		printer.classDef(0, def);
	}
	for (const Subroutine& sub : program.subroutines) {
		printer.subroutine(0, sub, false);
	}
	if (!program.statements.empty()) {
		printer.line(0, "Main");
		printer.block(1, program.statements);
	}
	return printer.out;
}
