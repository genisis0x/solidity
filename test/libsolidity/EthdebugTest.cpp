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
#include <test/libsolidity/util/Common.h>
#include <test/Common.h>

#include <libsolutil/CommonIO.h>
#include <libsolutil/JSON.h>
#include <libsolutil/StringUtils.h>

#include <liblangutil/DebugInfoSelection.h>

#include <libsolidity/interface/OptimiserSettings.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/throw_exception.hpp>

#include <stdexcept>

using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::frontend::test;
using namespace solidity::langutil;
using namespace solidity::util;
using namespace std::string_literals;

EthdebugTest::EthdebugTest(std::string const& _filename):
	EVMVersionRestrictedTestCase(_filename)
{
	m_source = m_reader.source();
	m_optimise = m_reader.boolSetting("optimize", false);
	m_optimiseYul = m_reader.boolSetting("optimize-yul", false);
	parseExpectations(m_reader.stream());
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

// ----- Expectation parsing -----

void EthdebugTest::parseExpectations(std::istream& _stream)
{
	std::string line;
	while (getline(_stream, line))
	{
		if (!boost::starts_with(line, "// "))
		{
			if (line == "//")
				continue; // blank comment line
			BOOST_THROW_EXCEPTION(std::runtime_error("Invalid expectation: expected \"// \" prefix in line: " + line));
		}

		std::string_view stripped(line);
		stripped.remove_prefix(3); // remove "// "

		// Skip blank lines
		if (stripped.empty() || stripped.find_first_not_of(" \t") == std::string_view::npos)
			continue;

		// Check if this is a multi-line value (line ends with ":" and no inline value after it)
		auto colonPos = stripped.rfind(':');
		if (colonPos != std::string_view::npos &&
			stripped.substr(colonPos + 1).find_first_not_of(" \t") == std::string_view::npos)
		{
			// Multi-line mode: path is everything before ':', value is a JSON
			// block on subsequent lines.
			Expectation exp = parsePathPart(stripped.substr(0, colonPos));
			exp.value = extractExpectationJSON(_stream);
			m_expectations.push_back(std::move(exp));
		}
		else
		{
			// Single-line mode: "path: value"
			m_expectations.push_back(parseExpectationLine(stripped));
		}
	}
}

std::string EthdebugTest::extractExpectationJSON(std::istream& _stream)
{
	std::string rawJSON;
	std::string line;
	while (getline(_stream, line))
	{
		std::string_view stripped;
		if (boost::starts_with(line, "// "))
			stripped = std::string_view(line).substr(3);
		else if (line == "//")
			stripped = "";
		else
			BOOST_THROW_EXCEPTION(std::runtime_error("Invalid expectation: expected \"// \" prefix in JSON block: " + line));

		rawJSON += stripped;
		rawJSON += "\n";

		if (boost::starts_with(stripped, "}") || boost::starts_with(stripped, "]"))
			break;
	}
	return rawJSON;
}

EthdebugTest::Expectation EthdebugTest::parseExpectationLine(std::string_view _line)
{
	// Split on ": " to get path-part and value
	auto colonPos = _line.find(": ");
	if (colonPos == std::string_view::npos)
		BOOST_THROW_EXCEPTION(std::runtime_error(
			"Missing ': ' separator in expectation line: "s.append(_line)
		));

	std::string_view pathPart = _line.substr(0, colonPos);
	std::string value(_line.substr(colonPos + 2));

	Expectation result = parsePathPart(pathPart);
	result.value = std::move(value);
	return result;
}

EthdebugTest::Expectation EthdebugTest::parsePathPart(std::string_view _pathPart)
{
	Expectation result;

	// Check for filter: "path | filter"
	auto pipePos = _pathPart.find(" | ");
	if (pipePos != std::string_view::npos)
	{
		result.filter = std::string(_pathPart.substr(pipePos + 3));
		_pathPart = _pathPart.substr(0, pipePos);
	}

	result.fullPath = std::string(_pathPart);

	// Split into output key + JSON path
	resolveOutputKey(_pathPart, result);
	return result;
}

void EthdebugTest::resolveOutputKey(std::string_view _path, Expectation& _expectation)
{
	if (_path.empty())
		BOOST_THROW_EXCEPTION(std::runtime_error("Empty path in expectation"));

	if (_path[0] == '.')
	{
		// Global path: ".compilation.compiler.name" -> outputKey=".compilation", jsonPath="compiler.name"
		// ".resources.compilation.sources" -> outputKey=".resources", jsonPath="compilation.sources"
		auto secondDot = _path.find('.', 1);
		if (secondDot == std::string_view::npos)
		{
			// Path is just ".compilation" or ".resources" with no sub-path
			_expectation.outputKey = std::string(_path);
			_expectation.jsonPath = "";
		}
		else
		{
			_expectation.outputKey = std::string(_path.substr(0, secondDot));
			_expectation.jsonPath = std::string(_path.substr(secondDot + 1));
		}
	}
	else
	{
		// Per-contract path: "C.creation.instructions[0].offset"
		// First segment = contract name, second segment = kind (creation/runtime/contract)
		auto firstDot = _path.find('.');
		if (firstDot == std::string_view::npos)
			BOOST_THROW_EXCEPTION(std::runtime_error(
				"Invalid path (expected at least ContractName.kind): "s.append(_path)
			));

		std::string_view contractName = _path.substr(0, firstDot);
		std::string_view rest = _path.substr(firstDot + 1);

		auto secondDot = rest.find('.');
		std::string_view kind;
		if (secondDot == std::string_view::npos)
		{
			kind = rest;
			_expectation.jsonPath = "";
		}
		else
		{
			kind = rest.substr(0, secondDot);
			_expectation.jsonPath = std::string(rest.substr(secondDot + 1));
		}

		_expectation.outputKey = std::string(contractName) + "." + std::string(kind);
	}
}

// ----- Path resolution & filters -----

std::vector<std::string> EthdebugTest::splitPath(std::string const& _path) const
{
	std::vector<std::string> segments;
	if (_path.empty())
		return segments;

	std::string current;
	for (size_t i = 0; i < _path.size(); ++i)
	{
		if (_path[i] == '.' && (current.empty() || current.back() != ']'))
		{
			// Split on dot, but not inside brackets
			if (!current.empty())
			{
				segments.push_back(std::move(current));
				current.clear();
			}
		}
		else
			current += _path[i];
	}
	if (!current.empty())
		segments.push_back(std::move(current));

	return segments;
}

std::optional<Json> EthdebugTest::resolvePath(Json const& _json, std::string const& _path) const
{
	if (_path.empty())
		return _json;

	Json current = _json;
	std::vector<std::string> segments = splitPath(_path);

	for (auto const& segment: segments)
	{
		// Check for array index: "key[N]"
		auto bracketPos = segment.find('[');
		if (bracketPos != std::string::npos)
		{
			std::string key = segment.substr(0, bracketPos);
			size_t closeBracket = segment.find(']', bracketPos);
			if (closeBracket == std::string::npos)
				return std::nullopt;
			size_t index = std::stoul(segment.substr(bracketPos + 1, closeBracket - bracketPos - 1));

			if (!key.empty())
			{
				if (!current.is_object() || !current.contains(key))
					return std::nullopt;
				current = current[key];
			}
			if (!current.is_array() || index >= current.size())
				return std::nullopt;
			current = current[static_cast<unsigned>(index)];
		}
		else
		{
			if (!current.is_object() || !current.contains(segment))
				return std::nullopt;
			current = current[segment];
		}
	}

	return current;
}

Json EthdebugTest::applyFilter(Json const& _json, std::string const& _filter) const
{
	if (_filter == "length")
	{
		if (_json.is_array())
			return Json(static_cast<uint64_t>(_json.size()));
		if (_json.is_object())
			return Json(static_cast<uint64_t>(_json.size()));
		if (_json.is_string())
			return Json(static_cast<uint64_t>(_json.get<std::string>().size()));
		solAssert(false, "Cannot apply 'length' filter to this JSON type");
	}
	else if (_filter == "type")
	{
		if (_json.is_null()) return Json("null");
		if (_json.is_boolean()) return Json("boolean");
		if (_json.is_number()) return Json("number");
		if (_json.is_string()) return Json("string");
		if (_json.is_array()) return Json("array");
		if (_json.is_object()) return Json("object");
	}
	else if (_filter == "keys")
	{
		solAssert(_json.is_object(), "Cannot apply 'keys' filter to non-object");
		Json result = Json::array();
		for (auto const& [key, _]: _json.items())
			result.push_back(key);
		return result;
	}
	else
		solAssert(false, "Unknown filter: " + _filter);

	return {};
}

// ----- Value formatting -----

std::string EthdebugTest::formatValue(Json const& _json) const
{
	if (_json.is_string())
		return _json.get<std::string>(); // unquoted
	if (_json.is_number_integer())
		return std::to_string(_json.get<int64_t>());
	if (_json.is_number_unsigned())
		return std::to_string(_json.get<uint64_t>());
	if (_json.is_boolean())
		return _json.get<bool>() ? "true" : "false";
	if (_json.is_null())
		return "null";
	// For objects and arrays, use compact JSON for single-line display
	return jsonCompactPrint(_json);
}

void EthdebugTest::applyPlaceholders(Json const& _expected, Json& _obtained) const
{
	if (_expected.is_string() && _expected.get<std::string>() == "<VERSION>")
	{
		_obtained = "<VERSION>";
		return;
	}

	if (_expected.is_object() && _obtained.is_object())
	{
		for (auto const& [key, expectedVal]: _expected.items())
			if (_obtained.contains(key))
				applyPlaceholders(expectedVal, _obtained[key]);
	}
	else if (_expected.is_array() && _obtained.is_array())
	{
		for (size_t i = 0; i < std::min(_expected.size(), _obtained.size()); ++i)
			applyPlaceholders(_expected[i], _obtained[i]);
	}
}

// ----- Output collection -----

std::map<std::string, Json> EthdebugTest::collectEthdebugOutput() const
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

// ----- Test execution -----

TestCase::TestResult EthdebugTest::run(std::ostream& _stream, std::string const& _linePrefix, bool _formatted)
{
	if (!runFramework(withPreamble(m_source), PipelineStage::Compilation))
	{
		printPrefixed(_stream, formatErrors(filteredErrors(), _formatted), _linePrefix);
		return TestResult::FatalError;
	}

	auto outputs = collectEthdebugOutput();
	bool allMatch = true;

	for (auto const& expectation: m_expectations)
	{
		auto it = outputs.find(expectation.outputKey);
		if (it == outputs.end())
		{
			allMatch = false;
			continue;
		}

		auto resolved = resolvePath(it->second, expectation.jsonPath);
		if (!resolved)
		{
			allMatch = false;
			continue;
		}

		Json value = *resolved;
		if (!expectation.filter.empty())
			value = applyFilter(value, expectation.filter);

		std::string obtained = formatValue(value);

		// Multi-line values: compare as parsed JSON (ignoring formatting)
		if (expectation.value.find('\n') != std::string::npos)
		{
			Json expectedJson;
			std::string errors;
			if (!jsonParseStrict(expectation.value, expectedJson, &errors))
			{
				allMatch = false;
				continue;
			}
			// Patch obtained JSON: replace any field that has a placeholder in expected
			Json patchedObtained = value;
			applyPlaceholders(expectedJson, patchedObtained);

			if (jsonPrint(patchedObtained, {JsonFormat::Pretty, 4}) !=
				jsonPrint(expectedJson, {JsonFormat::Pretty, 4}))
				allMatch = false;
		}
		else if (expectation.value == "<VERSION>")
		{
			// Single-line placeholder: matches any string value
			if (!value.is_string())
				allMatch = false;
		}
		else if (value.is_object() || value.is_array())
		{
			// Compound single-line value: parse expected as JSON and compare structurally
			Json expectedJson;
			std::string errors;
			if (!jsonParseStrict(expectation.value, expectedJson, &errors) ||
				value != expectedJson)
				allMatch = false;
		}
		else if (obtained != expectation.value)
			allMatch = false;
	}

	if (allMatch)
		return TestResult::Success;

	// On failure: print expected vs. obtained (like GasTest)
	_stream << _linePrefix << "Expected:" << std::endl;
	printPrefixed(_stream, formatExpectations(m_expectations), _linePrefix + "  ");
	_stream << _linePrefix << "Obtained:" << std::endl;
	printUpdatedExpectations(_stream, _linePrefix + "  ");
	return TestResult::Failure;
}

// ----- Printing -----

std::string EthdebugTest::formatExpectations(std::vector<Expectation> const& _expectations) const
{
	std::string output;
	for (auto const& exp: _expectations)
	{
		std::string pathPrefix = exp.fullPath;
		if (!exp.filter.empty())
			pathPrefix += " | " + exp.filter;

		if (exp.value.find('\n') != std::string::npos)
		{
			output += pathPrefix + ":\n";
			output += exp.value;
		}
		else
			output += pathPrefix + ": " + exp.value + "\n";
	}
	return output;
}

void EthdebugTest::printUpdatedExpectations(std::ostream& _stream, std::string const& _linePrefix) const
{
	auto outputs = collectEthdebugOutput();

	for (auto const& exp: m_expectations)
	{
		auto it = outputs.find(exp.outputKey);

		std::string pathPrefix = exp.fullPath;
		if (!exp.filter.empty())
			pathPrefix += " | " + exp.filter;

		if (it == outputs.end())
		{
			_stream << _linePrefix << pathPrefix << ": <SCOPE NOT FOUND>" << std::endl;
			continue;
		}

		auto resolved = resolvePath(it->second, exp.jsonPath);
		if (!resolved)
		{
			_stream << _linePrefix << pathPrefix << ": <PATH NOT FOUND>" << std::endl;
			continue;
		}

		Json value = *resolved;
		if (!exp.filter.empty())
			value = applyFilter(value, exp.filter);

		// Preserve placeholders: if the expectation used <VERSION>, emit it back
		// instead of the actual volatile value.
		if (exp.value == "<VERSION>")
		{
			_stream << _linePrefix << pathPrefix << ": <VERSION>" << std::endl;
			continue;
		}

		if (exp.value.find('\n') != std::string::npos)
		{
			// Multi-line: apply placeholders to obtained JSON before printing
			Json expectedJson;
			std::string errors;
			Json patchedObtained = value;
			if (jsonParseStrict(exp.value, expectedJson, &errors))
				applyPlaceholders(expectedJson, patchedObtained);

			std::string formatted = formatValue(patchedObtained);
			_stream << _linePrefix << pathPrefix << ":" << std::endl;
			printPrefixed(_stream, formatted, _linePrefix);
		}
		else
		{
			std::string formatted = formatValue(value);
			_stream << _linePrefix << pathPrefix << ": " << formatted << std::endl;
		}
	}
}
