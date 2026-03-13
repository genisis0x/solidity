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

#include <optional>
#include <test/Common.h>
#include <test/libsolidity/util/SoltestErrors.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

using namespace solidity::util;
using namespace solidity::test;

#define TEST_CASE_NAME (boost::unit_test::framework::current_test_case().p_name)

namespace solidity::frontend::test
{

BOOST_AUTO_TEST_SUITE(StandardJSONTests)

BOOST_AUTO_TEST_CASE(default_creation)
{
	json::StandardJSONInput input;

	BOOST_CHECK_EQUAL(input.settings.has_value(), false);
}

BOOST_AUTO_TEST_CASE(default_to_json)
{
	using namespace solidity::frontend::json;

	Json json{StandardJSONInput{}};

	BOOST_CHECK_EQUAL(json["language"], "Solidity");
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace solidity::frontend::test

