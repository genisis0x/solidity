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
/**
 * @date 2017
 * Enums for AST classes.
 */

#pragma once

#include <liblangutil/Exceptions.h>
#include <libsolidity/ast/ASTForward.h>

#include <string>

namespace solidity::frontend
{

/// Possible lookups for function resolving
enum class VirtualLookup { Static, Virtual, Super };

// How a function can mutate the EVM state.
enum class StateMutability { Pure, View, NonPayable, Payable };

/// State mutability names used for conversion from and to std::string.
static constexpr std::array<std::string, 4> STATE_MUTABILITY_NAMES  = { "pure", "view", "nonpayable", "payable" };

inline constexpr StateMutability stateMutabilityFromString(std::string const& _stateMutability)
{
	auto it = std::ranges::find(STATE_MUTABILITY_NAMES, _stateMutability);
	solAssert(it != STATE_MUTABILITY_NAMES.end(), "Unknown state mutability \"" + _stateMutability + "\"");
	return static_cast<StateMutability>(std::ranges::distance(STATE_MUTABILITY_NAMES.begin(), it));
}

inline constexpr std::string stateMutabilityToString(StateMutability const& _stateMutability)
{
	return STATE_MUTABILITY_NAMES[static_cast<std::size_t>(_stateMutability)];
}

/// Visibility ordered from restricted to unrestricted.
enum class Visibility { Default, Private, Internal, Public, External };

enum class Arithmetic { Checked, Wrapping };

class Type;

/// Container for function call parameter types & names
struct FuncCallArguments
{
	/// Types of arguments
	std::vector<Type const*> types;
	/// Names of the arguments if given, otherwise unset
	std::vector<ASTPointer<ASTString>> names;

	size_t numArguments() const { return types.size(); }
	size_t numNames() const { return names.size(); }
	bool hasNamedArguments() const { return !names.empty(); }
};

enum class ContractKind { Interface, Contract, Library };

}
