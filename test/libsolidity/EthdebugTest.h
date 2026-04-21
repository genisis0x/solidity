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

#include <test/libsolidity/AnalysisFramework.h>
#include <test/TestCase.h>
#include <libsolutil/JSON.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace solidity::frontend::test
{

class EthdebugTest: AnalysisFramework, public EVMVersionRestrictedTestCase
{
public:
	static std::unique_ptr<TestCase> create(Config const& _config)
	{ return std::make_unique<EthdebugTest>(_config.filename); }

	EthdebugTest(std::string const& _filename);

	TestResult run(std::ostream& _stream, std::string const& _linePrefix = "", bool _formatted = false) override;
	void printUpdatedExpectations(std::ostream& _stream, std::string const& _linePrefix) const override;

protected:
	void setupCompiler(CompilerStack& _compiler) override;

private:
	/// Represents a single path/value expectation.
	struct Expectation
	{
		std::string fullPath;   // original path as written, e.g. "C.creation.contract.name"
		std::string outputKey;  // resolved output key, e.g. "C.creation" or ".compilation"
		std::string jsonPath;   // path within the JSON, e.g. "contract.name"
		std::string filter;     // optional filter, e.g. "length"
		std::string value;      // expected value (could be multi-line JSON or a primitive)
	};

	void parseExpectations(std::istream& _stream);
	Expectation parseExpectationLine(std::string_view _line);
	Expectation parsePathPart(std::string_view _pathPart);
	void resolveOutputKey(std::string_view _path, Expectation& _expectation);
	std::string extractExpectationJSON(std::istream& _stream);

	/// Resolve a dotted path (with array indexing) within a JSON value.
	std::optional<Json> resolvePath(Json const& _json, std::string const& _path) const;

	/// Split a dotted path into segments, keeping [N] attached to the preceding key.
	std::vector<std::string> splitPath(std::string const& _path) const;

	/// Apply a filter (e.g. "length") to a JSON value.
	Json applyFilter(Json const& _json, std::string const& _filter) const;

	/// Format a JSON value as a comparable string (unquoted primitives, pretty-printed compounds).
	std::string formatValue(Json const& _json) const;

	/// Recursively replace values in _obtained with placeholders found in _expected.
	/// E.g. if _expected has "<VERSION>" at some path, _obtained's value there is overwritten.
	void applyPlaceholders(Json const& _expected, Json& _obtained) const;

	/// Collect ethdebug output from the compiler for all scopes.
	std::map<std::string, Json> collectEthdebugOutput() const;

	/// Format expectations as a printable string.
	std::string formatExpectations(std::vector<Expectation> const& _expectations) const;

	bool m_optimise = false;
	bool m_optimiseYul = false;
	std::vector<Expectation> m_expectations;
};

}
