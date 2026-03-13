/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include <libsolutil/FixedHash.h>
#include <libsolutil/JSON.h>

#include <liblangutil/EVMVersion.h>
#include <liblangutil/Exceptions.h>

#include <libsolidity/interface/Common.h>
#include <libsolidity/interface/DebugSettings.h>
#include <libsolidity/interface/OptimiserSettings.h>

#include <range/v3/algorithm.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

#include <optional>

using namespace solidity;
using namespace solidity::util;

namespace solidity::frontend::json
{

namespace input
{
	struct YulOptimizerDetails
	{
		/// Improve allocation of stack slots for variables, can free up stack slots early.
        /// Default: true if Yul optimizer is enabled.
		std::optional<bool> stackAllocation;
		/// Optimization step sequence. The general form of the value is "<main sequence>:<cleanup sequence>".
		/// If it does not contain the ':' delimiter, it is interpreted as the main
        /// sequence and the default is used for the cleanup sequence.
		/// Default: true.
		std::optional<std::string> optimizerSteps;
	};

	struct OptimizerDetails
	{
		// Peephole optimizer (opcode-based). Default: true.
		std::optional<bool> peephole = std::nullopt;
        // Inliner (opcode-based). Optional. Default: true when optimization is enabled.
        std::optional<bool> inliner = std::nullopt;
        // Unused JUMPDEST remover (opcode-based). Default: true.
        std::optional<bool> jumpdestRemover = std::nullopt;
        // Literal reordering (codegen-based). Default: true when optimization is enabled.
        std::optional<bool> orderLiterals = std::nullopt;
        // Block deduplicator (opcode-based). Default: true when optimization is enabled.
        std::optional<bool> deduplicate = std::nullopt;
        // Common subexpression elimination (opcode-based). Default: true when optimization is enabled.
        std::optional<bool> cse = std::nullopt;
        // Constant optimizer (opcode-based). Default: true when optimization is enabled.
        std::optional<bool> constantOptimizer = std::nullopt;
        // Unchecked loop increment (codegen-based). Default: true.
        std::optional<bool> simpleCounterForLoopUncheckedIncrement = std::nullopt;
        // Yul optimizer. Default: true when optimization is enabled.
		std::optional<bool> yul = std::nullopt;
		/// Tuning options for the Yul optimizer.
		std::optional<YulOptimizerDetails> yulDetails;
	};

	struct Optimizer
	{
		/// Turn on the optimizer. Default: false.
		std::optional<bool> enable;
		/// Optimize for how many times you intend to run the code. Default: 200.
		std::optional<size_t> runs;
		/// State of all optimizer components.
		std::optional<OptimizerDetails> details;
	};

	struct Debug
	{
		/// How to treat revert (and require) reason strings. Default: `RevertStrings::Default`.
		std::optional<RevertStrings> revertStrings = std::nullopt;
	};

	struct Metadata
	{
		/// The CBOR metadata is appended at the end of the bytecode by default. Default: true.
		/// Setting this to false omits the metadata from the runtime and deploy time code
		std::optional<bool> appendCBOR = std::nullopt;
		/// Use the given hash method for the metadata hash that is appended to the bytecode. Default: `MetadataHash::IPFS`.
		std::optional<MetadataHash> bytecodeHash = std::nullopt;
	};

	struct Settings
	{
		/// Experimental mode toggle. Default: false.
		std::optional<bool> experimental = std::nullopt;
		/// The optimizer settings.
		std::optional<Optimizer> optimizer = std::nullopt;
		/// Version of the EVM to compile for.
		std::optional<langutil::EVMVersion> evmVersion = std::nullopt;
		/// EVM Object Format version to compile for (experimental).
		std::optional<uint8_t> eofVersion = std::nullopt;
		/// Change compilation pipeline to go through the Yul intermediate representation. Default: false.
		std::optional<bool> viaIR = std::nullopt;
		/// Turn on SSA CFG-based code generation via the IR (experimental, implies viaIR: true). Default: true;
		std::optional<bool> viaSSACFG = std::nullopt;
		/// Debugging settings.
		std::optional<Debug> debug = std::nullopt;
		/// Metadata settings.
		std::optional<Metadata> metadata = std::nullopt;
	};

	///
	void to_json(Json&, YulOptimizerDetails const&);
	void to_json(Json&, OptimizerDetails const&);
	void to_json(Json&, Optimizer const&);
	void to_json(Json&, Debug const&);
	void to_json(Json&, Metadata const&);
	void to_json(Json&, Settings const&);
}

/**
 * The input the compiler is requested to compile with. It carries source and
 * compiler configuration.
 */
struct StandardJSONInput
{
	/// Source code which should be be compiled.
	std::map<std::string, std::string> sources = {};
	/// Information on which library is deployed where.
	std::map<std::string, util::h160> libraries = {};
	/// Contract name without a colon prefix.
	std::optional<std::string> contractName = std::nullopt;
	/// The compiler settings, e.g. EVM version, optimizer settings and Yul config.
	std::optional<input::Settings> settings = std::nullopt;
};

///
void to_json(Json&, StandardJSONInput const&);

}
