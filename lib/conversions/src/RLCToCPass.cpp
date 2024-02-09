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
#include "rlc/conversions/RLCToC.hpp"
#include "rlc/dialect/Operations.hpp"
#include "rlc/dialect/Passes.hpp"

namespace mlir::rlc
{
#define GEN_PASS_DEF_PRINTCHEADERPASS
#include "rlc/dialect/Passes.inc"

	struct PrintCHeaderPass: impl::PrintCHeaderPassBase<PrintCHeaderPass>
	{
		using impl::PrintCHeaderPassBase<PrintCHeaderPass>::PrintCHeaderPassBase;

		void runOnOperation() override { ::rlc::rlcToCHeader(getOperation(), *OS); }
	};
}	 // namespace mlir::rlc

namespace mlir::rlc
{
#define GEN_PASS_DEF_PRINTGODOTPASS
#include "rlc/dialect/Passes.inc"

	struct PrintGodotPass: impl::PrintGodotPassBase<PrintGodotPass>
	{
		using impl::PrintGodotPassBase<PrintGodotPass>::PrintGodotPassBase;

		void runOnOperation() override { ::rlc::rlcToGodot(getOperation(), *OS); }
	};
}	 // namespace mlir::rlc
