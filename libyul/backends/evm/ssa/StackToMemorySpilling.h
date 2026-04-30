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

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <range/v3/view/map.hpp>

#include <cstdint>
#include <map>

namespace solidity::yul::ssa
{
class SpilledVariables
{
public:

	void spill(InstId const _valueId)
	{
		bool const emplaced = m_spillSet.try_emplace(_valueId, m_currentSpillSlot).second;
		yulAssert(emplaced, fmt::format("can't spill a value ({}) twice", _valueId));
		++m_currentSpillSlot;
	}

	bool isSpilled(InstId const _valueId) const
	{
		return m_spillSet.contains(_valueId);
	}

	std::size_t numSpilled() const
	{
		return m_spillSet.size();
	}

	auto spilledValues() const
	{
		return m_spillSet | ranges::views::keys;
	}
private:
	std::uint32_t m_currentSpillSlot{0};
	std::map<InstId, std::uint32_t> m_spillSet;  // map valueId -> ssa-cfg-local slot
};
}
