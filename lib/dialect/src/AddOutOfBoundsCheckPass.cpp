/*
This file is part of the RLC project.

RLC is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.

RLC is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
RLC. If not, see <https://www.gnu.org/licenses/>.
*/
#include <cstddef>
#include <cstdint>

#include "llvm/Support/Casting.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "rlc/dialect/Dialect.h"
#include "rlc/dialect/Operations.hpp"
#include "rlc/dialect/Types.hpp"
#include "rlc/dialect/conversion/TypeConverter.h"

namespace mlir::rlc
{
#define GEN_PASS_DEF_ADDOUTOFBOUNDSCHECKPASS
#include "rlc/dialect/Passes.inc"
	struct AddOutOfBoundsCheckPass
			: public impl::AddOutOfBoundsCheckPassBase<AddOutOfBoundsCheckPass>
	{
		void getDependentDialects(mlir::DialectRegistry& registry) const override
		{
			registry.insert<mlir::rlc::RLCDialect>();
		}

		void runOnOperation() override
		{
			ModuleOp module = getOperation();

			module->walk([&](mlir::rlc::ArrayAccess op) { addOutOfBoundsCheck(op); });
		}

		void addOutOfBoundsCheck(mlir::rlc::ArrayAccess& op)
		{
			// if the accessed object is not an array, do nothing.
			auto array = op.getValue().getType().dyn_cast<mlir::rlc::ArrayType>();
			if (array == nullptr)
			{
				return;
			}

			// if we statically know it is a safe access, do not emit the check
			auto* definingOp = op.getMemberIndex().getDefiningOp();
			if (auto casted = mlir::dyn_cast_or_null<mlir::rlc::Constant>(definingOp);
					casted)
			{
				int64_t index = casted.getValue()
														.cast<mlir::IntegerAttr>()
														.getValue()
														.getZExtValue();
				if (index >= 0 and index < array.getArraySize())
					return;
			}

			mlir::OpBuilder builder(op);

			// construct the condition
			auto arraySizeConst = builder.create<mlir::rlc::Constant>(
					op->getLoc(), array.getArraySize());
			auto ge = builder.create<mlir::rlc::GreaterEqualOp>(
					op->getLoc(), op.getMemberIndex(), arraySizeConst.getResult());
			auto zero =
					builder.create<mlir::rlc::Constant>(op->getLoc(), (int64_t) 0);
			auto lt = builder.create<mlir::rlc::LessOp>(
					op->getLoc(), op.getMemberIndex(), zero);
			auto disj = builder.create<OrOp>(op->getLoc(), ge, lt);
			auto neg = builder.create<NotOp>(op.getLoc(), disj);

			// emit the assertion.
			builder.create<mlir::rlc::AssertOp>(op.getLoc(), neg);
		}
	};

}	 // namespace mlir::rlc
