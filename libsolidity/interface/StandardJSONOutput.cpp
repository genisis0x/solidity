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

#include <cstddef>
#include <libsolidity/interface/StandardJSONOutput.h>

#include <liblangutil/Exceptions.h>
#include <libsolutil/CommonData.h>
#include <libsolutil/JSON.h>
#include <libsolidity/ast/ASTEnums.h>

#include <range/v3/algorithm/transform.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

using namespace solidity::frontend;
using namespace solidity::frontend::json;
using namespace solidity::frontend::json::output;

ABI const& Contract::abi() const
{
	if (!m_abi)
		m_abi = m_raw.at("abi");
	return *m_abi;
}

std::string const& Contract::metadata() const
{
	if (!m_metadata)
		m_metadata = m_raw.at("metadata");
	return *m_metadata;
}

EVM const& Contract::evm() const
{
	if (!m_evm)
		m_evm = m_raw.at("evm");
	return *m_evm;
}

void output::from_json(Json const& _json, SourceLocation& _sourceLocation)
{
	_sourceLocation = SourceLocation{
		_json["file"],
		_json["start"],
		_json["end"],
		_json.contains("message") ? std::optional{_json.at("message")} : std::nullopt
	};
}

void output::from_json(Json const& _json, Error& _error)
{
	if (_json.contains("sourceLocation"))
		_error.sourceLocation = _json.at("sourceLocation");
	if (_json.contains("secondarySourceLocations") && _json.at("secondarySourceLocations").is_array())
		_error.secondarySourceLocations = _json.at("secondarySourceLocations");
	if (_json.contains("errorCode"))
		_error.errorCode = langutil::ErrorId{std::stoull(_json.at("errorCode").get<std::string>())};

	auto type = langutil::Error::parseErrorType(_json.at("type"));
	solAssert(type);

	_error.type = type.value();
	_error.message = _json.at("message");
}

void output::from_json(Json const& _json, Source& _source)
{
	_source.id = _json.at("id");
	if (_json.contains("ast"))
		_source.ast = _json.at("ast");
}

void output::from_json(Json const& _json, ABIParameter& _param)
{
    _param.name = _json.at("name").get<std::string>();
    _param.type = _json.at("type").get<std::string>();
    _param.internalType = _json.contains("internalType") ?
		std::optional{_json.at("internalType")} :
		std::nullopt;
    _param.indexed = _json.contains("indexed") ?
		std::optional{_json.at("indexed").get<bool>()} :
		std::nullopt;
	_param.components = _json.contains("components") ?
		std::optional{_json.at("components").get<std::vector<ABIParameter>>()} :
		std::nullopt;
}

void output::from_json(Json const& _json, ABIConstructor& _constructor)
{
	_constructor.stateMutability = stateMutabilityFromString(_json.at("stateMutability"));
    _constructor.inputs = _json.at("inputs").get<std::vector<ABIParameter>>();
}


void output::from_json(Json const& _json, ABIFunction& _function)
{
    _function.name = _json.at("name");
    _function.stateMutability = stateMutabilityFromString(_json.at("stateMutability"));;
    _function.inputs = _json.at("inputs").get<std::vector<ABIParameter>>();
    _function.outputs = _json.at("outputs").get<std::vector<ABIParameter>>();
}

void output::from_json(Json const& _json, ABIEvent& _event)
{
    _event.name = _json.at("name");
    _event.isAnonymous = _json.at("anonymous");
    _event.inputs = _json.at("inputs").get<std::vector<ABIParameter>>();
}

void output::from_json(Json const& _json, ABIError& _event)
{
    _event.name = _json.at("name");
    _event.inputs = _json.at("inputs").get<std::vector<ABIParameter>>();
}

void output::from_json(Json const& _json, ABIEntry& _entry)
{
    auto const type = _json.at("type").get<std::string>();
	if (type == "constructor")
		_entry = _json.get<ABIConstructor>();
	else if (type == "function")
		_entry = _json.get<ABIFunction>();
	else if (type == "event")
		_entry = _json.get<ABIEvent>();
	else if (type == "error")
		_entry = _json.get<ABIError>();
}

void output::from_json(Json const& _json, ByteOffset& _byteOffset)
{
	_byteOffset.start = _json.at("start").get<size_t>();
	_byteOffset.length = _json.at("length").get<size_t>();
}

void output::from_json(Json const& _json, Bytecode& _bytecode)
{
	_bytecode.object = util::fromHex(_json.at("object").get<std::string>());
	_bytecode.linkReferences = _json.at("linkReferences");
}

void output::from_json(Json const& _json, EVM& _evm)
{
	_evm.bytecode = _json.at("bytecode").get<Bytecode>();
	_evm.methodIdentifiers = _json.at("methodIdentifiers");
}

void output::from_json(Json const& _json, Contracts& _contracts)
{
    for (auto const& [source, contractsJson] : _json.items())
        for (auto const& [name, contractJson] : contractsJson.items())
            _contracts[source].push_back(output::Contract{name, contractJson});
}
