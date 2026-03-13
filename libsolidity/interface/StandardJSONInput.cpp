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
#include <libsolidity/interface/StandardJSONInput.h>

#include <liblangutil/Exceptions.h>
#include <libsolidity/interface/Common.h>

#include <range/v3/algorithm/transform.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/transform.hpp>

#include <optional>
using namespace solidity::frontend;
using namespace solidity::frontend::json;
using namespace solidity::frontend::json::input;

void input::to_json(Json& _json, YulOptimizerDetails const& _details)
{
	if (_details.stackAllocation)
		_json["stackAllocation"] = *_details.stackAllocation;
	if (_details.optimizerSteps)
		_json["optimizerSteps"] = *_details.optimizerSteps;
}

void input::to_json(Json& _json, OptimizerDetails const& _details)
{
	if (_details.peephole)
		_json["peephole"] = *_details.peephole;
	if (_details.orderLiterals)
		_json["orderLiterals"] = *_details.orderLiterals;
	if (_details.inliner)
		_json["inliner"] = *_details.inliner;
	if (_details.jumpdestRemover)
		_json["jumpdestRemover"] = *_details.jumpdestRemover;
	if (_details.deduplicate)
		_json["deduplicate"] = *_details.deduplicate;
	if (_details.cse)
		_json["cse"] = *_details.cse;
	if (_details.constantOptimizer)
		_json["constantOptimizer"] = *_details.constantOptimizer;
	if (_details.simpleCounterForLoopUncheckedIncrement)
		_json["simpleCounterForLoopUncheckedIncrement"] = *_details.simpleCounterForLoopUncheckedIncrement;
	if (_details.yul)
		_json["yul"] = *_details.yul;
	if (_details.yulDetails && _details.yul && *_details.yul)
		_json["yulDetails"] = *_details.yulDetails;
}

void input::to_json(Json& _json, Optimizer const& _optimizer)
{
	if (_optimizer.enable)
		_json["enabled"] = *_optimizer.enable;
	if (_optimizer.runs)
		_json["runs"] = *_optimizer.runs;
	if (_optimizer.details)
		_json["details"] = *_optimizer.details;
}

void input::to_json(Json& _json, Debug const& _debug)
{
	if (_debug.revertStrings)
    	_json["revertStrings"] = revertStringsToString(_debug.revertStrings.value());
}

void input::to_json(Json& _json, Metadata const& _metadata)
{
    if (_metadata.appendCBOR)
	{
		_json["appendCBOR"] = *_metadata.appendCBOR;
		if (*_metadata.appendCBOR && _metadata.bytecodeHash)
			_json["bytecodeHash"] = metadataHashToString(_metadata.bytecodeHash.value());
	}
}

void input::to_json(Json& _json, Settings const& _settings)
{
	if (_settings.optimizer)
		_json["optimizer"] = *_settings.optimizer;
	if (_settings.evmVersion)
		_json["evmVersion"] = (*_settings.evmVersion).name();
	if (_settings.eofVersion)
	    _json["eofVersion"] = *_settings.eofVersion;
    if (_settings.viaIR)
		_json["viaIR"] = *_settings.viaIR;
	if (_settings.viaSSACFG)
		_json["viaSSACFG"] = *_settings.viaSSACFG;
	if (_settings.debug)
		_json["debug"] = *_settings.debug;
	if (_settings.metadata)
		_json["metadata"] = *_settings.metadata;
}

void json::to_json(Json& _json, StandardJSONInput const& _input)
{
	_json["language"] = "Solidity";

	for (auto& [name, source] : _input.sources)
		_json["sources"][name]["content"] = source;

	if (_input.settings)
	{
		_json["settings"] = *_input.settings;
		for (auto& [name, source] : _input.sources)
		{
			_json["settings"]["libraries"][name] = _input.libraries | ranges::views::transform([](auto const& entry) {
				auto const& [libraryName, address] = entry;
				auto parts = libraryName | ranges::views::split(':') | ranges::to<std::vector<std::string>>();
				return std::pair{parts.back(), "0x" + address.hex()};
			}) | ranges::to<std::map>();
		}
		_json["settings"]["outputSelection"]["*"]["*"] = Json::array({"*"});
		_json["settings"]["outputSelection"]["*"][""] = Json::array({"ast"});
	}
}

