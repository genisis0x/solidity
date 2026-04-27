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
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace solidity::frontend::test
{

/// Test harness for JSON-output tests driven via the same expectation DSL used by
/// EthdebugTest. The DSL supports:
///   - Single-line expectations:     `// path: value`
///   - Multi-line JSON expectations: `// path:` followed by a `// `-prefixed JSON block
///   - Filters:                      `// path | length: 5`, `path | type: object`, `path | keys: [...]`
///   - Array indexing in paths:      `C.creation.instructions[0].offset`
///   - Placeholders:                 `<VERSION>` (default; override placeholders() to add more)
///
/// Subclasses provide:
///   - setupCompiler() — compiler driving (defaults to AnalysisFramework's behavior)
///   - collectScopes() — the named JSON scopes that paths resolve against
///   - splitNonGlobalPath() — how non-global paths split into (scope key, JSON path)
///   - placeholders() — additional string placeholders accepted at JSON leaves
class StandardJSONTest: public AnalysisFramework, public EVMVersionRestrictedTestCase
{
public:
	StandardJSONTest(std::string const& _filename);

	TestResult run(std::ostream& _stream, std::string const& _linePrefix = "", bool _formatted = false) override;
	void printUpdatedExpectations(std::ostream& _stream, std::string const& _linePrefix) const override;

protected:
	/// Represents a single path/value expectation.
	struct Expectation
	{
		std::string fullPath;   // original path as written, e.g. "C.creation.contract.name"
		std::string filter;     // optional filter, e.g. "length"
		std::string value;      // expected value (could be multi-line JSON or a primitive)
	};

	/// Subclass hook: produce the named JSON scopes after a successful compilation.
	/// Keys are typically ".global" (leading dot) or non-global keys like "ContractName.kind".
	virtual std::map<std::string, Json> collectScopes() const = 0;

	/// Subclass hook: split a non-global dotted path into (scope key, JSON sub-path).
	/// Default takes the first segment as the scope key and the rest as the JSON path.
	/// Override to consume additional segments (e.g. ethdebug uses "ContractName.kind").
	virtual std::pair<std::string, std::string> splitNonGlobalPath(std::string_view _path) const;

	/// Subclass hook for additional placeholders that match any string at a JSON leaf.
	virtual std::set<std::string> placeholders() const { return {"<VERSION>"}; }

private:
	void parseExpectations(std::istream& _stream);
	Expectation parseExpectationLine(std::string_view _line);
	Expectation parsePathPart(std::string_view _pathPart);
	/// Splits @p _path into (scope key, JSON sub-path). Calls splitNonGlobalPath() for non-global
	/// paths, so it must only be invoked after construction (otherwise virtual dispatch resolves
	/// to the base implementation rather than the derived class's override).
	std::pair<std::string, std::string> resolveOutputKey(std::string_view _path) const;
	std::string extractExpectationJSON(std::istream& _stream);

	std::optional<Json> resolvePath(Json const& _json, std::string const& _path) const;
	std::vector<std::string> splitPath(std::string const& _path) const;
	Json applyFilter(Json const& _json, std::string const& _filter) const;
	std::string formatValue(Json const& _json) const;
	void applyPlaceholders(Json const& _expected, Json& _obtained) const;
	bool isPlaceholder(std::string const& _value) const;
	std::string formatExpectations(std::vector<Expectation> const& _expectations) const;

	std::vector<Expectation> m_expectations;
};

}
