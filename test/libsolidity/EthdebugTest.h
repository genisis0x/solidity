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

#include <test/libsolidity/StandardJSONTest.h>

namespace solidity::frontend::test
{

class EthdebugTest: public StandardJSONTest
{
public:
	static std::unique_ptr<TestCase> create(Config const& _config)
	{ return std::make_unique<EthdebugTest>(_config.filename); }

	EthdebugTest(std::string const& _filename);

protected:
	void setupCompiler(CompilerStack& _compiler) override;
	std::map<std::string, Json> collectScopes() const override;
	std::pair<std::string, std::string> splitNonGlobalPath(std::string_view _path) const override;

private:
	bool m_optimise = false;
	bool m_optimiseYul = false;
};

}
