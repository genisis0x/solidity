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

#include <test/libsolidity/StandardJSONTest.h>
#include <test/libsolidity/util/Common.h>

#include <libsolutil/CommonIO.h>
#include <libsolutil/JSON.h>
#include <libsolutil/StringUtils.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/throw_exception.hpp>

#include <stdexcept>

using namespace solidity;
using namespace solidity::frontend;
using namespace solidity::frontend::test;
using namespace solidity::util;
using namespace std::string_literals;

StandardJSONTest::StandardJSONTest(std::string const& _filename):
	EVMVersionRestrictedTestCase(_filename)
{
	m_source = m_reader.source();
	parseExpectations(m_reader.stream());
}

// ----- Expectation parsing -----

void StandardJSONTest::parseExpectations(std::istream& _stream)
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

std::string StandardJSONTest::extractExpectationJSON(std::istream& _stream)
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

StandardJSONTest::Expectation StandardJSONTest::parseExpectationLine(std::string_view _line)
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

StandardJSONTest::Expectation StandardJSONTest::parsePathPart(std::string_view _pathPart)
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
	return result;
}

std::pair<std::string, std::string> StandardJSONTest::resolveOutputKey(std::string_view _path) const
{
	if (_path.empty())
		BOOST_THROW_EXCEPTION(std::runtime_error("Empty path in expectation"));

	if (_path[0] == '.')
	{
		// Global path: ".compilation.compiler.name" -> outputKey=".compilation", jsonPath="compiler.name"
		// ".resources.compilation.sources" -> outputKey=".resources", jsonPath="compilation.sources"
		auto secondDot = _path.find('.', 1);
		if (secondDot == std::string_view::npos)
			return {std::string(_path), ""};
		return {std::string(_path.substr(0, secondDot)), std::string(_path.substr(secondDot + 1))};
	}
	return splitNonGlobalPath(_path);
}

std::pair<std::string, std::string> StandardJSONTest::splitNonGlobalPath(std::string_view _path) const
{
	// Default: first segment is the scope key, the rest is the JSON path.
	auto firstDot = _path.find('.');
	if (firstDot == std::string_view::npos)
		return {std::string(_path), ""};
	return {std::string(_path.substr(0, firstDot)), std::string(_path.substr(firstDot + 1))};
}

// ----- Path resolution & filters -----

std::vector<std::string> StandardJSONTest::splitPath(std::string const& _path) const
{
	std::vector<std::string> segments;
	if (_path.empty())
		return segments;

	std::string current;
	for (char c: _path)
	{
		if (c == '.')
		{
			if (!current.empty())
			{
				segments.push_back(std::move(current));
				current.clear();
			}
		}
		else
			current += c;
	}
	if (!current.empty())
		segments.push_back(std::move(current));

	return segments;
}

std::optional<Json> StandardJSONTest::resolvePath(Json const& _json, std::string const& _path) const
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

Json StandardJSONTest::applyFilter(Json const& _json, std::string const& _filter) const
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

std::string StandardJSONTest::formatValue(Json const& _json) const
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

bool StandardJSONTest::isPlaceholder(std::string const& _value) const
{
	return placeholders().count(_value) > 0;
}

void StandardJSONTest::applyPlaceholders(Json const& _expected, Json& _obtained) const
{
	if (_expected.is_string() && isPlaceholder(_expected.get<std::string>()))
	{
		_obtained = _expected.get<std::string>();
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

// ----- Test execution -----

TestCase::TestResult StandardJSONTest::run(std::ostream& _stream, std::string const& _linePrefix, bool _formatted)
{
	if (!runFramework(withPreamble(m_source), PipelineStage::Compilation))
	{
		printPrefixed(_stream, formatErrors(filteredErrors(), _formatted), _linePrefix);
		return TestResult::FatalError;
	}

	auto outputs = collectScopes();
	bool allMatch = true;

	for (auto const& expectation: m_expectations)
	{
		auto [outputKey, jsonPath] = resolveOutputKey(expectation.fullPath);
		auto it = outputs.find(outputKey);
		if (it == outputs.end())
		{
			allMatch = false;
			continue;
		}

		auto resolved = resolvePath(it->second, jsonPath);
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
		else if (isPlaceholder(expectation.value))
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

std::string StandardJSONTest::formatExpectations(std::vector<Expectation> const& _expectations) const
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

void StandardJSONTest::printUpdatedExpectations(std::ostream& _stream, std::string const& _linePrefix) const
{
	auto outputs = collectScopes();

	for (auto const& exp: m_expectations)
	{
		auto [outputKey, jsonPath] = resolveOutputKey(exp.fullPath);
		auto it = outputs.find(outputKey);

		std::string pathPrefix = exp.fullPath;
		if (!exp.filter.empty())
			pathPrefix += " | " + exp.filter;

		if (it == outputs.end())
		{
			_stream << _linePrefix << pathPrefix << ": <SCOPE NOT FOUND>" << std::endl;
			continue;
		}

		auto resolved = resolvePath(it->second, jsonPath);
		if (!resolved)
		{
			_stream << _linePrefix << pathPrefix << ": <PATH NOT FOUND>" << std::endl;
			continue;
		}

		Json value = *resolved;
		if (!exp.filter.empty())
			value = applyFilter(value, exp.filter);

		// Preserve placeholders: if the expectation used a placeholder, emit it back
		// instead of the actual volatile value.
		if (isPlaceholder(exp.value))
		{
			_stream << _linePrefix << pathPrefix << ": " << exp.value << std::endl;
			continue;
		}

		if (exp.value.find('\n') != std::string::npos)
		{
			// Multi-line: apply placeholders to obtained JSON before printing,
			// and pretty-print so the diff against the expected block is readable.
			Json expectedJson;
			std::string errors;
			Json patchedObtained = value;
			if (jsonParseStrict(exp.value, expectedJson, &errors))
				applyPlaceholders(expectedJson, patchedObtained);

			std::string formatted = jsonPrint(patchedObtained, {JsonFormat::Pretty, 4});
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
