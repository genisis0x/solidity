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

#include <liblangutil/Exceptions.h>

#include <array>

namespace solidity::frontend
{

/// Use the given hash method for the metadata hash that is appended to the bytecode.
enum class MetadataHash { IPFS, Bzzr1, None };

/// Metadata hash names used for conversion from and to std::string.
static constexpr std::array<std::string, 3> METADATA_HASH_NAMES = { "ipfs", "bzzr1", "none" };

inline constexpr MetadataHash metadataHashFromString(std::string const& _metadataHash)
{
	auto it = std::ranges::find(METADATA_HASH_NAMES, _metadataHash);
	solAssert(it != METADATA_HASH_NAMES.end(), "Unknown metadata hash: \"" + _metadataHash + "\"");
	return static_cast<MetadataHash>(std::ranges::distance(METADATA_HASH_NAMES.begin(), it));
}

inline constexpr std::string metadataHashToString(MetadataHash const& _metadataHash)
{
	return METADATA_HASH_NAMES[static_cast<std::size_t>(_metadataHash)];
}

}




