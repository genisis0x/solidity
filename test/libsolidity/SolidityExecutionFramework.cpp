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
/**
 * @author Christian <c@ethdev.com>
 * @date 2016
 * Framework for executing Solidity contracts and testing them against C++ implementation.
 */

#include "liblangutil/SourceLocation.h"
#include "libsolidity/interface/StandardJSONInput.h"
#include <memory>
#include <range/v3/view/transform.hpp>
#include <test/libsolidity/SolidityExecutionFramework.h>
#include <test/libsolidity/util/Common.h>
#include <test/libsolidity/util/StandardJSONCompiler.h>
#include <test/libsolidity/util/SoltestErrors.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/Exceptions.h>
#include <liblangutil/SourceReferenceFormatter.h>
#include <libyul/Exceptions.h>

#include <boost/test/framework.hpp>

#include <range/v3/algorithm.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/view/filter.hpp>

#include <iostream>

using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::frontend::test;
using namespace solidity::langutil;
using namespace solidity::test;

namespace
{
	std::shared_ptr<const langutil::Error> convertError(json::output::Error const& _error)
	{
		return std::make_shared<const langutil::Error>(_error.toInternalError());
	}
}

bytes SolidityExecutionFramework::multiSourceCompileContract(
	std::map<std::string, std::string> const& _sourceCode,
	std::string const& _contractName,
	std::map<std::string, Address> const& _libraryAddresses,
	std::optional<std::string> const& _mainSourceName
)
{
	if (_mainSourceName.has_value())
		solAssert(_sourceCode.find(_mainSourceName.value()) != _sourceCode.end(), "");

	m_compilerInput = json::StandardJSONInput{
		.sources = withPreamble(
			_sourceCode,
			solidity::test::CommonOptions::get().useABIEncoderV1 // _addAbicoderV1Pragma
		),
		.libraries = _libraryAddresses,
		.settings = json::input::Settings{
			.optimizer = json::input::Optimizer{
				.enable = false,
				.runs = m_optimiserSettings.expectedExecutionsPerDeployment,
				.details = json::input::OptimizerDetails{
					.peephole = m_optimiserSettings.runPeephole,
					.inliner = m_optimiserSettings.runInliner,
					.jumpdestRemover = m_optimiserSettings.runJumpdestRemover,
					.orderLiterals = m_optimiserSettings.runOrderLiterals,
					.deduplicate = m_optimiserSettings.runDeduplicate,
					.cse = m_optimiserSettings.runCSE,
					.constantOptimizer = m_optimiserSettings.runConstantOptimiser,
					.simpleCounterForLoopUncheckedIncrement = m_optimiserSettings.simpleCounterForLoopUncheckedIncrement,
					.yul = m_optimiserSettings.runYulOptimiser,
					.yulDetails = json::input::YulOptimizerDetails{
						.stackAllocation = m_optimiserSettings.optimizeStackAllocation,
						.optimizerSteps = m_optimiserSettings.yulOptimiserSteps,
					}
				}
			},
			.evmVersion = m_evmVersion,
			.eofVersion = m_eofVersion,
			.viaIR = m_compileViaYul,
			.debug = json::input::Debug{
				.revertStrings = m_revertStrings
			},
			.metadata = json::input::Metadata{
				.appendCBOR = m_appendCBORMetadata,
				.bytecodeHash = m_metadataHash,
			}
		}
	};
	StandardJSONOutputExt const& output = m_compiler.compile(m_compilerInput);

	if (!output.success())
	{
		// The testing framework expects an exception for
		// "unimplemented" yul IR generation.
		auto codeGenError = ranges::find_if(output.errors(), [](auto const& e) {
			return e.type == Error::Type::CodeGenerationError;
		});

		if (m_compileViaYul && codeGenError != output.errors().end())
			BOOST_THROW_EXCEPTION(*convertError(*codeGenError));

		auto errors = output.errors() | ranges::views::transform(convertError) | ranges::to<langutil::ErrorList>();
		for (auto const& [name, code]: m_compilerInput.sources)
			fmt::print("\n{}\n", SourceReferenceFormatter::formatErrorInformation(
				errors,
				SingletonCharStreamProvider{CharStream{code, name}},
				true,
				false
			));

		BOOST_ERROR("Compiling contract failed");
	}

	// Construct `ContractName` with the contract name given, and use `_mainSourceName`
	// if the contract's name source prefix is empty.
	auto const [sourceName, contractName, _] = decomposeContractName(_contractName);
	ContractName lookupName{
		sourceName.empty() ? _mainSourceName.value_or("") : sourceName,
		contractName
	};

	auto const* contract = output.contract(lookupName);
	soltestAssert(contract);
	soltestAssert(contract->evm().bytecode.linkReferences.empty());

	if (m_showMetadata)
		std::cout << "metadata: " << contract->metadata() << std::endl;

	return contract->evm().bytecode.object;
}

bytes SolidityExecutionFramework::compileContract(
	std::string const& _sourceCode,
	std::string const& _contractName,
	std::map<std::string, Address> const& _libraryAddresses
)
{
	return multiSourceCompileContract(
		{{"", _sourceCode}},
		_contractName,
		_libraryAddresses,
		std::nullopt
	);
}
