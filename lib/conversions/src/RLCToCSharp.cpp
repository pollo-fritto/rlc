/*
Copyright 2024 Massimo Fioravanti

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

	 http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "llvm/ADT/TypeSwitch.h"
#include "mlir/IR/BuiltinDialect.h"
#include "rlc/conversions/CSharpConversions.hpp"
#include "rlc/conversions/RLCToC.hpp"
#include "rlc/dialect/MemberFunctionsTable.hpp"
#include "rlc/dialect/Operations.hpp"
#include "rlc/dialect/Passes.hpp"
#include "rlc/dialect/Types.hpp"
#include "rlc/dialect/Visits.hpp"
#include "rlc/dialect/conversion/TypeConverter.h"

namespace mlir::rlc
{

	static bool isCSharpBuiltinType(mlir::Type type)
	{
		if (auto casted = mlir::dyn_cast<mlir::rlc::FrameType>(type))
			return isCSharpBuiltinType(casted.getUnderlying());
		if (auto casted = mlir::dyn_cast<mlir::rlc::ContextType>(type))
			return isCSharpBuiltinType(casted.getUnderlying());
		return mlir::isa<mlir::rlc::IntegerType>(type) or
					 mlir::isa<mlir::rlc::BoolType>(type) or
					 mlir::isa<mlir::rlc::OwningPtrType>(type) or
					 mlir::isa<mlir::rlc::ReferenceType>(type) or
					 mlir::isa<mlir::rlc::FloatType>(type) or
					 mlir::isa<mlir::rlc::StringLiteralType>(type);
	}

	static void registerTypeConversionRaw(TypeSerializer& matcher)
	{
		matcher.add([](mlir::rlc::IntegerLiteralType type,
									 llvm::raw_string_ostream& OS) { OS << type.getValue(); });
		matcher.add([](mlir::rlc::VoidType type, llvm::raw_string_ostream& OS) {
			OS << "void";
		});
		matcher.add([&](mlir::rlc::AliasType type, llvm::raw_string_ostream& OS) {
			OS << type.getName();
		});
		matcher.add([&](mlir::rlc::FrameType type, llvm::raw_string_ostream& OS) {
			OS << matcher.convert(type.getUnderlying());
		});
		matcher.add([&](mlir::rlc::ContextType type, llvm::raw_string_ostream& OS) {
			OS << matcher.convert(type.getUnderlying());
		});
		matcher.add(
				[&](mlir::rlc::AlternativeType type, llvm::raw_string_ostream& OS) {
					OS << type.getMangledName() << ".Content";
				});

		matcher.add([](mlir::rlc::IntegerType type, llvm::raw_string_ostream& OS) {
			if (type.getSize() == 64)
				OS << "long";
			else
				OS << "sbyte";
		});
		matcher.add([](mlir::rlc::FloatType type, llvm::raw_string_ostream& OS) {
			OS << "double";
		});
		matcher.add([](mlir::rlc::BoolType type, llvm::raw_string_ostream& OS) {
			OS << "bool";
		});
		matcher.add([&](mlir::rlc::ClassType type, llvm::raw_string_ostream& OS) {
			OS << type.mangledName() << ".Content";
		});
		matcher.add([](mlir::rlc::StringLiteralType type,
									 llvm::raw_string_ostream& OS) { OS << "char*"; });
		matcher.add([&](mlir::rlc::ArrayType type, llvm::raw_string_ostream& OS) {
			OS << typeToMangled(type) << ".Content";
		});
		matcher.add(
				[&](mlir::rlc::OwningPtrType type, llvm::raw_string_ostream& OS) {
					OS << matcher.convert(type.getUnderlying()) << "*";
				});
		matcher.add(
				[&](mlir::rlc::ReferenceType type, llvm::raw_string_ostream& OS) {
					OS << matcher.convert(type.getUnderlying()) << "*";
				});
	}

	static void registerTypeConversion(TypeSerializer& matcher)
	{
		matcher.add([](mlir::rlc::IntegerLiteralType type,
									 llvm::raw_string_ostream& OS) { OS << type.getValue(); });
		matcher.add([](mlir::rlc::VoidType type, llvm::raw_string_ostream& OS) {
			OS << "void";
		});
		matcher.add([&](mlir::rlc::AliasType type, llvm::raw_string_ostream& OS) {
			OS << type.getName();
		});
		matcher.add([&](mlir::rlc::FrameType type, llvm::raw_string_ostream& OS) {
			OS << matcher.convert(type.getUnderlying());
		});
		matcher.add([&](mlir::rlc::ContextType type, llvm::raw_string_ostream& OS) {
			OS << matcher.convert(type.getUnderlying());
		});
		matcher.add(
				[&](mlir::rlc::AlternativeType type, llvm::raw_string_ostream& OS) {
					OS << type.getMangledName();
				});

		matcher.add([](mlir::rlc::IntegerType type, llvm::raw_string_ostream& OS) {
			if (type.getSize() == 64)
				OS << "long";
			else
				OS << "sbyte";
		});
		matcher.add([](mlir::rlc::FloatType type, llvm::raw_string_ostream& OS) {
			OS << "double";
		});
		matcher.add([](mlir::rlc::BoolType type, llvm::raw_string_ostream& OS) {
			OS << "bool";
		});
		matcher.add([&](mlir::rlc::ClassType type, llvm::raw_string_ostream& OS) {
			OS << type.mangledName();
		});
		matcher.add([](mlir::rlc::StringLiteralType type,
									 llvm::raw_string_ostream& OS) { OS << "char*"; });
		matcher.add([&](mlir::rlc::ArrayType type, llvm::raw_string_ostream& OS) {
			OS << typeToMangled(type);
		});
		matcher.add(
				[&](mlir::rlc::OwningPtrType type, llvm::raw_string_ostream& OS) {
					OS << matcher.convert(type.getUnderlying()) << "*";
				});
		matcher.add(
				[&](mlir::rlc::ReferenceType type, llvm::raw_string_ostream& OS) {
					OS << matcher.convert(type.getUnderlying());
				});
	}

	static void emitPrelude(StreamWriter& writer)
	{
		writer.writenl("using System;");
		writer.writenl("using System.Runtime.InteropServices;").endLine();
	}

	static mlir::Type getResultType(mlir::FunctionType type)
	{
		if (type.getNumResults() == 0)
			return mlir::rlc::VoidType::get(type.getContext());
		return type.getResult(0);
	}

	static size_t depthOfReference(mlir::Type type)
	{
		auto dereferencedReturnType = type;
		size_t derefToEmit = 0;
		while (auto casted =
							 mlir::dyn_cast<mlir::rlc::ReferenceType>(dereferencedReturnType))
		{
			dereferencedReturnType = casted.getUnderlying();
			derefToEmit++;
		}
		return derefToEmit;
	}

	static mlir::Type dereferenceType(mlir::Type type)
	{
		auto dereferencedReturnType = type;
		while (auto casted =
							 mlir::dyn_cast<mlir::rlc::ReferenceType>(dereferencedReturnType))
			dereferencedReturnType = casted.getUnderlying();
		return dereferencedReturnType;
	}

	static void writeName(llvm::StringRef name, StreamWriter& writer)
	{
		if (name != "out")
		{
			writer.write(name);
			return;
		}
		writer.write("_" + name);
	}

	static void writeFunctionArgs(
			mlir::TypeRange types,
			llvm::ArrayRef<llvm::StringRef> args,
			mlir::Type returnType,
			mlir::rlc::StreamWriter& writer,
			bool isBuiltinDeclaration,
			bool isMemberFunction = false)
	{
		writer.write("(");
		if (not mlir::isa<mlir::rlc::VoidType>(returnType))
		{
			if (isCSharpBuiltinType(returnType) or isBuiltinDeclaration)
				writer.write("ref ");
			if (isBuiltinDeclaration)
				writer.writeType(returnType, 1);
			else
				writer.writeType(dereferenceType(returnType));

			writer.write(" __result");
			if (not args.empty() or (args.size() == 1 and isMemberFunction))
				writer.write(", ");
		}
		size_t index = isMemberFunction;
		const int toDrop = int(isMemberFunction);
		for (auto [type, name] : llvm::drop_begin(llvm::zip(types, args), toDrop))
		{
			if (isBuiltinDeclaration)
				writer.write("ref ");
			writer.writeType(type, isBuiltinDeclaration);
			writer.write(" ");
			writeName(name, writer);
			if (++index != args.size())
				writer.write(", ");
		}
		writer.write(")");
	}

	static void declareFunction(
			llvm::StringRef mangledName,
			mlir::TypeRange types,
			llvm::ArrayRef<llvm::StringRef> args,
			mlir::Type returnType,
			mlir::rlc::StreamWriter& writer,
			bool isMac,
			bool isWindows,
			llvm::SmallVector<std::string>& declaredFunNames)
	{
		writer.write("public delegate void Delegate", mangledName);
		writeFunctionArgs(types, args, returnType, writer, true);
		writer.writenl(";");
		writer.writenl(
				"public static Delegate", mangledName, " ", mangledName, ";");

		declaredFunNames.push_back(mangledName.str());
	}

	static void emitReturnVariable(mlir::Type returnType, StreamWriter& writer)
	{
		if (not mlir::isa<mlir::rlc::VoidType>(returnType))
		{
			if (isCSharpBuiltinType(returnType))
			{
				writer.writeType(returnType, 1);
				writer.write(" __result");
				if (mlir::isa<mlir::rlc::ReferenceType>(returnType) or
						mlir::isa<mlir::rlc::OwningPtrType>(returnType) or
						mlir::isa<mlir::rlc::StringLiteralType>(returnType))
					writer.writenl(" = null;");
				else if (mlir::isa<mlir::rlc::BoolType>(returnType))
				{
					writer.writenl(" = false;");
				}
				else
				{
					writer.writenl(" = 0;");
				}
			}
			else
			{
				writer.writeType(returnType);
				writer.write(" __result");
				writer.write(" = new ");
				writer.writeType(returnType);
				writer.write("();");
			}
		}
	}

	static void emitMemberFunctionsWrapper(
			llvm::StringRef mangledName,
			llvm::StringRef name,
			mlir::TypeRange types,
			llvm::ArrayRef<llvm::StringRef> args,
			mlir::Type returnType,
			mlir::rlc::StreamWriter& writer)
	{
		writer.write("public ");
		writer.writeType(returnType);
		writer.write(" ", name);
		writeFunctionArgs(
				types,
				args,
				mlir::rlc::VoidType::get(returnType.getContext()),
				writer,
				false,
				true);
		writer.writenl("{");

		{
			auto _ = writer.indent();

			emitReturnVariable(returnType, writer);
			writer.write("RLCNative.", mangledName, "(");
			if (not mlir::isa<mlir::rlc::VoidType>(returnType))
			{
				if (isCSharpBuiltinType(returnType))
				{
					writer.write("ref __result");
				}
				else
				{
					writer.write("ref ");
					if (depthOfReference(returnType) != 1 and
							depthOfReference(returnType) != 0)
						abort();
					if (depthOfReference(returnType) == 0)
					{
						writer.write("*");
					}
					writer.write("__result.__content");
				}

				writer.write(", ");
			}
			writer.write("ref *this.__content");
			if (args.size() != 1)
				writer.write(", ");
			for (auto arg : llvm::drop_begin(llvm::enumerate(args), 1))
			{
				writer.write("ref ");
				if (not isCSharpBuiltinType(types[arg.index()]))
					writer.write("*");
				writer.write(arg.value());
				if (not isCSharpBuiltinType(types[arg.index()]))
					writer.write(".__content");
				if (arg.index() + 1 != args.size())
					writer.write(", ");
			}

			writer.writenl(");");

			if (not mlir::isa<mlir::rlc::VoidType>(returnType))
			{
				writer.write("return ");
				if (depthOfReference(returnType) != 0)
				{
					if (isCSharpBuiltinType(dereferenceType(returnType)))
					{
						writer.writenl("* __result;");
					}
					else
					{
						writer.write("new ");
						writer.writeType(returnType);
						writer.writenl("(__result);");
					}
				}
				else
				{
					writer.writenl("__result;");
				}
			}
		}

		writer.writenl("}").endLine();
	}

	static void emitFreeFunctionsWrapper(
			llvm::StringRef mangledName,
			llvm::StringRef name,
			mlir::TypeRange types,
			llvm::ArrayRef<llvm::StringRef> args,
			mlir::Type returnType,
			mlir::rlc::StreamWriter& writer)
	{
		writer.write("public static ");
		writer.writeType(returnType);
		writer.write(" ", name);
		writeFunctionArgs(
				types,
				args,
				mlir::rlc::VoidType::get(returnType.getContext()),
				writer,
				false);
		writer.writenl("{");

		{
			auto _ = writer.indent();

			emitReturnVariable(returnType, writer);
			writer.write("RLCNative.", mangledName, "(");
			if (not mlir::isa<mlir::rlc::VoidType>(returnType))
			{
				writer.write("ref ");

				if (not isCSharpBuiltinType(returnType))
					writer.write("*");
				writer.write("__result");
				if (not isCSharpBuiltinType(returnType))
					writer.write(".__content");
				if (not args.empty())
					writer.write(", ");
			}
			for (auto arg : llvm::enumerate(args))
			{
				writer.write("ref ");

				if (not isCSharpBuiltinType(types[arg.index()]))
					writer.write("*");
				writeName(arg.value(), writer);
				if (not isCSharpBuiltinType(types[arg.index()]))
					writer.write(".__content");
				if (arg.index() + 1 != args.size())
				{
					writer.write(", ");
				}
			}

			writer.writenl(");");

			if (not mlir::isa<mlir::rlc::VoidType>(returnType))
			{
				writer.write("return");
				size_t depth = depthOfReference(returnType);
				for (size_t i = 0; i != depth; i++)
					writer.write("*");
				writer.writenl(" __result;");
			}
		}

		writer.writenl("}").endLine();
	}

	/**
	 * Emits the declaration for all functions
	 */
	class CSharpFunctionDeclarationMatcher
	{
		private:
		bool isMac;
		bool isWindows;
		llvm::SmallVector<std::string>& declaredFunNames;

		public:
		CSharpFunctionDeclarationMatcher(
				bool isMac,
				bool isWindows,
				llvm::SmallVector<std::string>& declaredFunNames)
				: isMac(isMac), isWindows(isWindows), declaredFunNames(declaredFunNames)
		{
		}
		void apply(mlir::rlc::FunctionOp op, mlir::rlc::StreamWriter& writer)
		{
			auto _ = writer.indent();
			if (op.isInternal())
				return;
			declareFunction(
					op.getMangledName(),
					op.getFunctionType().getInputs(),
					op.getInfo().getArgNames(),
					getResultType(op.getFunctionType()),
					writer,
					isMac,
					isWindows,
					declaredFunNames);

			if (not op.getPrecondition().empty())
				declareFunction(
						op.getCanFunctionMangledName(),
						op.getFunctionType().getInputs(),
						op.getInfo().getArgNames(),
						mlir::rlc::BoolType::get(op.getContext()),
						writer,
						isMac,
						isWindows,
						declaredFunNames);
		}
	};

	class CSharpActionDeclarationMatcher
	{
		private:
		mlir::rlc::ModuleBuilder& builder;
		bool isMac;
		bool isWindows;
		llvm::SmallVector<std::string>& declaredFunNames;

		public:
		CSharpActionDeclarationMatcher(
				mlir::rlc::ModuleBuilder& builder,
				bool isMac,
				bool isWindows,
				llvm::SmallVector<std::string>& declaredFunNames)
				: builder(builder),
					isMac(isMac),
					isWindows(isWindows),
					declaredFunNames(declaredFunNames)
		{
		}
		void apply(mlir::rlc::ActionFunction op, mlir::rlc::StreamWriter& writer)
		{
			auto _ = writer.indent();
			if (op.isInternal())
				return;

			declareFunction(
					op.getMangledName(),
					op.getFunctionType().getInputs(),
					op.getArgNames(),
					getResultType(op.getFunctionType()),
					writer,
					isMac,
					isWindows,
					declaredFunNames);
			if (not op.getPrecondition().empty())
			{
				declareFunction(
						op.getCanFunctionMangledName(),
						op.getFunctionType().getInputs(),
						op.getArgNames(),
						mlir::rlc::BoolType::get(op.getContext()),
						writer,
						isMac,
						isWindows,
						declaredFunNames);
			}

			for (auto value : op.getActions())
			{
				mlir::Operation* statement =
						builder.actionFunctionValueToActionStatement(value).front();
				auto actionStatement =
						mlir::cast<mlir::rlc::ActionStatement>(statement);
				llvm::SmallVector<llvm::StringRef> argNames = { "self" };
				for (auto arg : actionStatement.getInfo().getArguments())
					argNames.push_back(arg.getName());

				auto fType = mlir::cast<mlir::FunctionType>(value.getType());
				auto mangled = mangledName(actionStatement.getName(), true, fType);

				declareFunction(
						mangled,
						fType.getInputs(),
						argNames,
						getResultType(fType),
						writer,
						isMac,
						isWindows,
						declaredFunNames);

				auto canDoType = mlir::FunctionType::get(
						fType.getContext(),
						fType.getInputs(),
						{ mlir::rlc::BoolType::get(fType.getContext()) });

				declareFunction(
						mangledName(
								"can_" + actionStatement.getName().str(), true, canDoType),
						canDoType.getInputs(),
						argNames,
						getResultType(canDoType),
						writer,
						isMac,
						isWindows,
						declaredFunNames);
			}

			auto canFType = mlir::FunctionType::get(
					op.getContext(),
					{ op.getClassType() },
					{ mlir::rlc::BoolType::get(op.getContext()) });

			declareFunction(
					mangledName("is_done", true, canFType),
					canFType.getInputs(),
					{ "self" },
					getResultType(canFType),
					writer,
					isMac,
					isWindows,
					declaredFunNames);
		}
	};

	class CSharpActionWrappersMatcher
	{
		public:
		void apply(mlir::rlc::ActionFunction op, mlir::rlc::StreamWriter& writer)
		{
			auto _ = writer.indent();
			if (op.isInternal() or op.getIsMemberFunction())
				return;
			emitFreeFunctionsWrapper(
					op.getMangledName(),
					op.getUnmangledName(),
					op.getMainActionType().getInputs(),
					op.getInfo().getArgNames(),
					getResultType(op.getFunctionType()),
					writer);

			if (not op.getPrecondition().empty())
				emitFreeFunctionsWrapper(
						op.getCanFunctionMangledName(),
						"can_" + op.getUnmangledName().str(),
						op.getFunctionType().getInputs(),
						op.getInfo().getArgNames(),
						mlir::rlc::BoolType::get(op.getContext()),
						writer);
		}
	};

	/**
	 * Emits the wrapper for all free functions
	 */
	class CSharpFunctionWrappersMatcher
	{
		public:
		void apply(mlir::rlc::FunctionOp op, mlir::rlc::StreamWriter& writer)
		{
			auto _ = writer.indent();
			if (op.isInternal() or op.getIsMemberFunction())
				return;
			emitFreeFunctionsWrapper(
					op.getMangledName(),
					op.getUnmangledName(),
					op.getFunctionType().getInputs(),
					op.getInfo().getArgNames(),
					getResultType(op.getFunctionType()),
					writer);

			if (not op.getPrecondition().empty())
				emitFreeFunctionsWrapper(
						op.getCanFunctionMangledName(),
						"can_" + op.getUnmangledName().str(),
						op.getFunctionType().getInputs(),
						op.getInfo().getArgNames(),
						mlir::rlc::BoolType::get(op.getContext()),
						writer);
		}
	};

	static void emitAlternativeMembers(
			mlir::rlc::AlternativeType type, StreamWriter& writer)
	{
		auto _ = writer.indent();
		for (auto memberType : llvm::enumerate(type.getUnderlying()))
		{
			writer.write("[FieldOffset(0)]");
			writer.write("public ");
			writer.writeType(memberType.value());
			if (not isCSharpBuiltinType(memberType.value()))
			{
				writer.write(".Content");
			}
			writer.writenl(" arg", memberType.index(), ";");
		}
	}

	static void emitClassMembers(mlir::rlc::ClassType type, StreamWriter& writer)
	{
		auto _ = writer.indent();
		for (auto [memberType, memberName] :
				 llvm::zip(type.getMemberTypes(), type.getMemberNames()))
		{
			if (memberName.starts_with("_"))
				writer.write("private ");
			else
				writer.write("public ");
			writer.writeType(memberType, 1);
			writer.writenl(" ", memberName, ";");
		}
	}

	static void emitMemberFunction(
			mlir::Type type,
			StreamWriter& writer,
			mlir::rlc::FunctionOp memberFunction)
	{
		emitMemberFunctionsWrapper(
				memberFunction.getMangledName(),
				memberFunction.getUnmangledName(),
				memberFunction.getFunctionType().getInputs(),
				memberFunction.getArgNames(),
				getResultType(memberFunction.getType()),
				writer);
		if (not memberFunction.getPrecondition().empty())
			emitMemberFunctionsWrapper(
					memberFunction.getCanFunctionMangledName(),
					"can_" + memberFunction.getUnmangledName().str(),
					memberFunction.getFunctionType().getInputs(),
					memberFunction.getArgNames(),
					mlir::rlc::BoolType::get(memberFunction.getContext()),
					writer);
	}

	static void emitSpecialFunctions(
			mlir::Type type, StreamWriter& writer, MemberFunctionsTable& table)
	{
		auto _ = writer.indent();

		auto name = mlir::rlc::typeToMangled(type);
		writer.writenl("public ", name, "(", name, ".Content* referred) {");
		writer.indentOnce(1);
		writer.writenl("owning = false;");
		writer.indentOnce(1);
		writer.writenl("__content = referred;");
		writer.write("}").endLine();

		writer.writenl("public ", name, "() {");
		writer.indentOnce(1);
		writer.writenl(
				"__content = (",
				name,
				".Content*) Marshal.AllocHGlobal(sizeof(",
				name,
				".Content));");
		writer.indentOnce(1);
		writer.writenl("owning = true;");
		writer.indentOnce(1);
		if (not table.isTriviallyInitializable(type))
		{
			writer.writenl(
					"RLCNative.",
					mangledName(
							"init",
							true,
							mlir::FunctionType::get(type.getContext(), { type }, {})),
					"(ref *this.__content);");
		}

		writer.write("}").endLine();
		writer.writenl("~", name, "() {");
		writer.indentOnce(1);
		writer.writenl("if (!owning)");
		writer.indentOnce(2);
		writer.writenl("return;");
		writer.indentOnce(1);
		if (not table.isTriviallyDestructible(type))
		{
			writer.indentOnce(1);
			writer.writenl(
					"RLCNative.",
					mangledName(
							"drop",
							true,
							mlir::FunctionType::get(type.getContext(), { type }, {})),
					"(ref *this.__content);");
		}
		writer.writenl("Marshal.FreeHGlobal((IntPtr)__content);");

		writer.write("}").endLine();

		if (not table.isTriviallyCopiable(type))
		{
			writer.write("public void assign(", name, " other) {");
			writer.indentOnce(1);
			writer.writenl(
					"RLCNative.",
					mangledName(
							"assign",
							true,
							mlir::FunctionType::get(type.getContext(), { type, type }, {})),
					"(ref *this.__content, ref *other.__content);");
			writer.writenl("}").endLine();
		}
	}

	static void emitGetterSetters(
			mlir::rlc::AlternativeType type, StreamWriter& writer)
	{
		auto _ = writer.indent();
		for (auto alternative : llvm::enumerate(type.getUnderlying()))
		{
			writer.write("public ");
			writer.writeType(alternative.value());
			if (isCSharpBuiltinType(alternative.value()))
				writer.write("?");
			writer.write(" get_");
			writer.writeType(alternative.value());
			writer.write(
					" { get => __content->__active_index== ", alternative.index(), " ? ");
			if (not isCSharpBuiltinType(alternative.value()))
			{
				writer.write("new ");
				writer.writeType(alternative.value());
				writer.write("(&(");
			}
			else
			{
				writer.write("(");
				writer.writeType(alternative.value());
				writer.write("?)");
			}

			writer.write("__content->__alternatives.arg");
			writer.write(alternative.index());
			if (not isCSharpBuiltinType(alternative.value()))
			{
				writer.write("))");
			}
			writer.write(" : null");
			// writer.write("; set => __content->", name, " = ");
			// if (not isCSharpBuiltinType(type))
			//{
			// writer.write("*");
			//}

			// writer.write("value");

			// if (not isCSharpBuiltinType(type))
			//{
			// writer.write(".__content");
			//}
			writer.writenl(";}").endLine();
		}
	}

	static void emitGetterSetters(mlir::rlc::ClassType type, StreamWriter& writer)
	{
		auto _ = writer.indent();
		for (auto [type, name] :
				 llvm::zip(type.getMemberTypes(), type.getMemberNames()))
		{
			if (name.starts_with("_"))
			{
				continue;
			}
			writer.write("public ");
			writer.writeType(type);
			writer.write(" ", name, " { get => ");
			if (not isCSharpBuiltinType(type))
			{
				writer.write("new ");
				writer.writeType(type);
				writer.write("(&");
			}

			writer.write("__content->", name);
			if (not isCSharpBuiltinType(type))
			{
				writer.write(")");
			}
			writer.write("; set => __content->", name, " = ");
			if (not isCSharpBuiltinType(type))
			{
				writer.write("*");
			}

			writer.write("value");

			if (not isCSharpBuiltinType(type))
			{
				writer.write(".__content");
			}
			writer.writenl(";}").endLine();
		}
	}

	static void emitArrayDecl(
			mlir::rlc::ArrayType type,
			StreamWriter& writer,
			MemberFunctionsTable& table,
			size_t typeSize,
			size_t elementSize)
	{
		writer.write("public unsafe class ");
		writer.writeType(type);
		writer.writenl("{");
		auto _ = writer.indent();

		{
			writer.writenl("public Content* __content;");
			writer.writenl("private bool owning;");
			writer.writenl("[StructLayout(LayoutKind.Sequential)]");
			writer.writenl("public struct Content {");
			auto _ = writer.indent();
			writer.write("public fixed byte");
			writer.write(" __content");
			writer.writenl("[", typeSize, "];");
			writer.writenl("}").endLine();

			writer.write("public ");
			if (isCSharpBuiltinType(type.getUnderlying()))
			{
				writer.write("ref ");
			}
			writer.writeType(type.getUnderlying());
			writer.writenl(" this [int index] {");
			writer.writenl("get {");
			writer.writenl(
					"if ((((uint) index) >= ",
					type.getArraySize(),
					")) throw new ArgumentOutOfRangeException(nameof(index));");
			writer.writenl("return ");
			if (isCSharpBuiltinType(type.getUnderlying()))
			{
				writer.write("ref (*(((");
				writer.writeType(type.getUnderlying());
				writer.write("*) __content) + index));");
			}
			else
			{
				writer.write("new ");
				writer.writeType(type.getUnderlying());
				writer.write("((((");
				writer.writeType(type.getUnderlying(), 1);
				writer.write("*) __content) + index));");
			}
			writer.writenl("}");
			writer.writenl("}");
		}

		emitSpecialFunctions(type, writer, table);

		writer.writenl("}").endLine();
	}

	static void emitAliasDecl(
			mlir::rlc::AliasType type,
			StreamWriter& writer,
			MemberFunctionsTable& table)
	{
		writer.write("using ", type.getName(), " = ");
		writer.writeType(type.getUnderlying());
		writer.writenl(";");
	}

	static void emitAlternativeDecl(
			mlir::rlc::AlternativeType type,
			StreamWriter& writer,
			MemberFunctionsTable& table)
	{
		writer.writenl("unsafe public class ", type.getMangledName(), "{");

		{
			auto _ = writer.indent();
			writer.writenl("public Content* __content;");
			writer.writenl("private bool owning;");
			writer.writenl("[StructLayout(LayoutKind.Explicit)]");
			writer.writenl("public struct Alternatives {");
			emitAlternativeMembers(type, writer);
			writer.writenl("}");

			writer.writenl("[StructLayout(LayoutKind.Sequential)]");
			writer.writenl("public struct Content {");
			writer.writenl("public Alternatives __alternatives;");
			writer.writenl("public long __active_index;");
			writer.writenl("}");
		}
		emitGetterSetters(type, writer);
		for (auto memberFunction : table.getMemberFunctionsOf(type))
		{
			auto _ = writer.indent();
			emitMemberFunction(type, writer, memberFunction);
		}
		emitSpecialFunctions(type, writer, table);

		writer.writenl("}").endLine();
	}

	static void emitClassContent(
			mlir::rlc::ClassType type,
			StreamWriter& writer,
			MemberFunctionsTable& table)
	{
		writer.writenl("unsafe public class ", type.mangledName(), "{");

		{
			auto _ = writer.indent();
			writer.writenl("public Content* __content;");
			writer.writenl("private bool owning;");
			writer.writenl("[StructLayout(LayoutKind.Sequential)]");
			writer.writenl("public struct Content {");
			emitClassMembers(type, writer);
			writer.writenl("}");
		}
		emitGetterSetters(type, writer);
		emitSpecialFunctions(type, writer, table);
	}

	static void emitActionDecl(
			mlir::rlc::ClassType type,
			StreamWriter& writer,
			MemberFunctionsTable& table,
			mlir::rlc::ModuleBuilder& builder)
	{
		emitClassContent(type, writer, table);

		auto op = mlir::cast<mlir::rlc::ActionFunction>(
				builder.getActionOf(type).getDefiningOp());

		auto _ = writer.indent();
		for (auto value : op.getActions())
		{
			mlir::Operation* statement =
					builder.actionFunctionValueToActionStatement(value).front();
			auto actionStatement = mlir::cast<mlir::rlc::ActionStatement>(statement);
			llvm::SmallVector<llvm::StringRef> argNames = { "self" };
			for (auto arg : actionStatement.getInfo().getArguments())
				argNames.push_back(arg.getName());

			auto fType = mlir::cast<mlir::FunctionType>(value.getType());
			auto mangled = mangledName(actionStatement.getName(), true, fType);

			emitMemberFunctionsWrapper(
					mangled,
					actionStatement.getName(),
					fType.getInputs(),
					argNames,
					getResultType(fType),
					writer);

			auto canDoType = mlir::FunctionType::get(
					fType.getContext(),
					fType.getInputs(),
					{ mlir::rlc::BoolType::get(fType.getContext()) });
			emitMemberFunctionsWrapper(
					mangledName(
							"can_" + actionStatement.getName().str(), true, canDoType),
					"can_" + actionStatement.getName().str(),
					canDoType.getInputs(),
					argNames,
					getResultType(canDoType),
					writer);
		}

		auto canFType = mlir::FunctionType::get(
				op.getContext(),
				{ op.getClassType() },
				{ mlir::rlc::BoolType::get(op.getContext()) });
		emitMemberFunctionsWrapper(
				mangledName("is_done", true, canFType),
				"is_done",
				{ op.getClassType() },
				{ "self" },
				getResultType(canFType),
				writer);

		writer.writenl("}").endLine();
	}

	static void emitClassDecl(
			mlir::rlc::ClassType type,
			StreamWriter& writer,
			MemberFunctionsTable& table,
			mlir::rlc::EnumDeclarationOp enumDecl)
	{
		emitClassContent(type, writer, table);
		for (auto memberFunction : table.getMemberFunctionsOf(type))
		{
			auto _ = writer.indent();
			emitMemberFunction(type, writer, memberFunction);
		}

		if (enumDecl)
		{
			auto _ = writer.indent();
			for (auto value : llvm::enumerate(
							 enumDecl.getBody().getOps<mlir::rlc::EnumFieldDeclarationOp>()))
			{
				writer.write(
						"public static ",
						enumDecl.getName(),
						" ",
						value.value().getName(),
						"() {");
				auto _ = writer.indent();
				writer.writenl(
						enumDecl.getName(), " __result = new ", enumDecl.getName(), "();");
				writer.writenl("__result.value = ", value.index(), ";");
				writer.writenl("return __result;");
				writer.writenl("}").endLine();
			}
		}

		writer.writenl("}").endLine();
	}

	static void emitSetTearDown(
			llvm::SmallVector<std::string>& declaredFunNames, StreamWriter& writer)
	{
		writer.writenl("internal static string SharedLibExtension =>");
		writer.writenl(
				" RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? \".dll\" :");
		writer.writenl(
				"RuntimeInformation.IsOSPlatform(OSPlatform.OSX)     ? \".dylib\" :");
		writer.writenl("/* default to Linux */ \".so\";");
		writer.writenl("private static IntPtr _lib;");
		auto _ = writer.indent();
		writer.writenl("public static void setup(string libName) {");
		{
			writer.write("_lib = RLCNative.LoadLibrary(libName);");
			writer.write(
					"if (_lib == IntPtr.Zero) throw new Exception(\"Could not find "
					"library \" + libName );");
			auto _ = writer.indent();
			for (auto& exposedSymbol : declaredFunNames)
			{
				writer.writenl(
						"IntPtr ",
						exposedSymbol,
						"_ptr = GetProcAddress(_lib, \"",
						exposedSymbol,
						"\");");
				writer.writenl(
						"if (",
						exposedSymbol,
						"_ptr == IntPtr.Zero) throw new Exception(\"Could not find symbol ",
						exposedSymbol,
						"\");");
				writer.writenl(
						exposedSymbol,
						" = Marshal.GetDelegateForFunctionPointer<Delegate",
						exposedSymbol,
						">(",
						exposedSymbol,
						"_ptr);");
			}
		}
		writer.writenl("}").endLine();

		writer.writenl("public static void teardown() {");
		{
			writer.writenl("if (_lib == IntPtr.Zero) return;");
			auto _ = writer.indent();
			for (auto& exposedSymbol : declaredFunNames)
			{
				writer.writenl(exposedSymbol, " = null;");
			}

			writer.write("RLCNative.FreeLibrary(_lib);");
			writer.writenl("_lib = IntPtr.Zero;");
		}
		writer.writenl("}").endLine();
	}

	static void emitDLLImporters(bool isMac, bool isWindows, StreamWriter& writer)
	{
		if (isMac)
		{
			writer.writenl("const string LIBDL = \"libSystem.B.dylib\";");
			writer.writenl("const int RTLD_NOW = 2;");
			writer.writenl("[DllImport(LIBDL)] static extern IntPtr dlopen (string "
										 "path, int flags);");
			writer.writenl(
					"[DllImport(LIBDL)] static extern int    dlclose(IntPtr handle);");
			writer.writenl("[DllImport(LIBDL)] static extern IntPtr dlsym  (IntPtr "
										 "handle, string name);");
			writer.writenl(
					"static IntPtr LoadLibrary (string p) => dlopen (p, RTLD_NOW);");
			writer.writenl(
					"static bool   FreeLibrary (IntPtr h)  { dlclose(h); return true; }");
			writer.writenl(
					"static IntPtr GetProcAddress(IntPtr h,string n)=>dlsym(h,n);");
		}
		else if (isWindows)
		{
			writer.writenl("const string KERNEL = \"kernel32\"\n");
			writer.writenl("[DllImport(KERNEL, SetLastError = true)] static extern "
										 "IntPtr LoadLibrary(string path) ");
			writer.writenl("[DllImport(KERNEL, SetLastError = true)] static extern "
										 "bool FreeLibrary(IntPtr hModule);");
			writer.writenl("[DllImport(KERNEL)]                      static extern "
										 "IntPtr GetProcAddress(IntPtr h, string name);");
		}
		else
		{
			writer.writenl("const string LIBDL = \"libdl.so.2\";");
			writer.writenl("const int RTLD_NOW = 2;");
			writer.writenl("[DllImport(LIBDL)] static extern IntPtr dlopen (string "
										 "path, int flags);");
			writer.writenl(
					"[DllImport(LIBDL)] static extern int    dlclose(IntPtr handle);");
			writer.writenl("[DllImport(LIBDL)] static extern IntPtr dlsym  (IntPtr "
										 "handle, string name);");
			writer.writenl(
					"[DllImport(LIBDL, CharSet = CharSet.Ansi, ExactSpelling = true)]");
			writer.writenl("static extern IntPtr dlerror();");
			writer.writenl(
					"static IntPtr LoadLibrary (string p) => dlopen (p, RTLD_NOW);");
			writer.writenl("static string DlLastError()");
			writer.writenl("{");
			writer.writenl("    IntPtr p = dlerror();");
			writer.writenl(
					"    return p != IntPtr.Zero ? Marshal.PtrToStringAnsi(p) : null;");
			writer.writenl("		}");
			writer.writenl(
					"static bool   FreeLibrary (IntPtr h)  { dlclose(h); return true; }");
			writer.writenl(
					"static IntPtr GetProcAddress(IntPtr h,string n)=>dlsym(h,n);");
		}
	}

	static size_t getSizeTypeInBytes(
			const mlir::DataLayout& dl,
			mlir::Type type,
			mlir::TypeConverter& converterToLLVMIR)
	{
		return dl.getTypeSize(converterToLLVMIR.convertType(type));
	}

#define GEN_PASS_DEF_PRINTCSHARPPASS
#include "rlc/dialect/Passes.inc"

	struct PrintCSharpPass: impl::PrintCSharpPassBase<PrintCSharpPass>
	{
		using impl::PrintCSharpPassBase<PrintCSharpPass>::PrintCSharpPassBase;

		void runOnOperation() override
		{
			PatternMatcher matcher(*OS);
			MemberFunctionsTable table(getOperation());
			mlir::rlc::ModuleBuilder builder(getOperation());
			mlir::TypeConverter converter;
			mlir::rlc::registerConversions(converter, getOperation());

			llvm::SmallVector<std::string> declaredFunNames;
			const auto& dl = mlir::DataLayout::closest(getOperation());

			emitPrelude(matcher.getWriter());
			matcher.addTypeSerializer();
			registerTypeConversion(matcher.getWriter().getTypeSerializer());
			registerTypeConversionRaw(matcher.getWriter().getTypeSerializer(1));
			matcher.getWriter().writenl("public unsafe class RLCNative {");
			emitDLLImporters(isMac, isWindows, matcher.getWriter());
			matcher.add<CSharpFunctionDeclarationMatcher>(
					isMac, isWindows, declaredFunNames);
			matcher.add<CSharpActionDeclarationMatcher>(
					builder, isMac, isWindows, declaredFunNames);
			matcher.apply(getOperation());
			emitSetTearDown(declaredFunNames, matcher.getWriter());
			matcher.getWriter().writenl("}").endLine();

			matcher.clearMatchers();
			matcher.getWriter().writenl("unsafe class RLC {");

			matcher.add<CSharpFunctionWrappersMatcher>();
			matcher.add<CSharpActionWrappersMatcher>();
			matcher.apply(getOperation());
			matcher.getWriter().writenl("}").endLine();

			llvm::StringMap<mlir::rlc::EnumDeclarationOp> enums;
			for (auto op : getOperation().getOps<mlir::rlc::EnumDeclarationOp>())
			{
				enums[op.getName()] = op;
			}

			for (auto type : ::rlc::postOrderTypes(getOperation()))
			{
				if (auto casted = mlir::dyn_cast<mlir::rlc::ClassType>(type))
				{
					if (builder.isClassOfAction(casted))

						emitActionDecl(casted, matcher.getWriter(), table, builder);
					else
						emitClassDecl(
								casted,
								matcher.getWriter(),
								table,
								enums.count(casted.getName()) ? enums[casted.getName()]
																							: nullptr);
				}
				else if (auto casted = mlir::dyn_cast<mlir::rlc::AlternativeType>(type))
				{
					emitAlternativeDecl(casted, matcher.getWriter(), table);
				}
				else if (auto casted = mlir::dyn_cast<mlir::rlc::AliasType>(type))
				{
					emitAliasDecl(casted, matcher.getWriter(), table);
				}
				else if (auto casted = mlir::dyn_cast<mlir::rlc::ArrayType>(type))
				{
					emitArrayDecl(
							casted,
							matcher.getWriter(),
							table,
							getSizeTypeInBytes(dl, casted, converter),
							getSizeTypeInBytes(dl, casted.getUnderlying(), converter));
				}
			}
		}
	};
}	 // namespace mlir::rlc
