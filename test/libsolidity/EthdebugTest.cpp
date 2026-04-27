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

#include <test/libsolidity/EthdebugTest.h>
#include <test/Common.h>

#include <liblangutil/DebugInfoSelection.h>

#include <libsolidity/interface/OptimiserSettings.h>

#include <boost/throw_exception.hpp>

#include <stdexcept>

using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::frontend::test;
using namespace solidity::langutil;
using namespace std::string_literals;

EthdebugTest::EthdebugTest(std::string const& _filename):
	StandardJSONTest(_filename)
{
	m_optimise = m_reader.boolSetting("optimize", false);
	m_optimiseYul = m_reader.boolSetting("optimize-yul", false);
}

void EthdebugTest::setupCompiler(CompilerStack& _compiler)
{
	AnalysisFramework::setupCompiler(_compiler);

	// ethdebug requires these
	_compiler.setViaIR(true);
	_compiler.setExperimental(true);

	// Enable ethdebug debug info
	DebugInfoSelection selection = DebugInfoSelection::Default();
	selection.enable("ethdebug");
	_compiler.selectDebugInfo(selection);

	// Disable metadata to avoid volatile output
	_compiler.setMetadataFormat(CompilerStack::MetadataFormat::NoMetadata);

	// Apply optimizer settings from test file
	OptimiserSettings settings = m_optimise ? OptimiserSettings::standard() : OptimiserSettings::minimal();
	if (m_optimiseYul)
		settings.runYulOptimiser = true;
	_compiler.setOptimiserSettings(settings);
}

std::map<std::string, Json> EthdebugTest::collectScopes() const
{
	std::map<std::string, Json> result;

	if (compiler().state() < CompilerStack::State::CompilationSuccessful)
		return result;

	// Global outputs (leading dot = global)
	result[".resources"] = compiler().ethdebug();
	result[".compilation"] = compiler().ethdebugCompilation();

	// Per-contract outputs
	for (std::string const& contractName: compiler().contractNames())
	{
		// contractName is "source.sol:ContractName" format
		size_t colon = contractName.rfind(':');
		std::string shortName = (colon != std::string::npos)
			? contractName.substr(colon + 1)
			: contractName;

		Json creation = compiler().ethdebug(contractName);
		Json runtime = compiler().ethdebugRuntime(contractName);

		result[shortName + ".creation"] = creation;
		result[shortName + ".runtime"] = runtime;

		// Shortcut: "C.contract" -> the contract sub-object from creation
		if (!creation.is_null() && creation.contains("contract"))
			result[shortName + ".contract"] = creation["contract"];
	}

	return result;
}

std::pair<std::string, std::string> EthdebugTest::splitNonGlobalPath(std::string_view _path) const
{
	// ethdebug per-contract paths have shape: "ContractName.kind.subPath"
	// where kind is one of "creation", "runtime", "contract".
	auto firstDot = _path.find('.');
	if (firstDot == std::string_view::npos)
		BOOST_THROW_EXCEPTION(std::runtime_error(
			"Invalid path (expected at least ContractName.kind): "s.append(_path)
		));

	std::string_view contractName = _path.substr(0, firstDot);
	std::string_view rest = _path.substr(firstDot + 1);

	auto secondDot = rest.find('.');
	if (secondDot == std::string_view::npos)
		return {std::string(contractName) + "." + std::string(rest), ""};

	return {
		std::string(contractName) + "." + std::string(rest.substr(0, secondDot)),
		std::string(rest.substr(secondDot + 1))
	};
}
