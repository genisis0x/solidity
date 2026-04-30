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

#include <libyul/backends/evm/ssa/transform/UnreachableBlockCleaner.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libsolutil/Visitor.h>

#include <cstdint>
#include <deque>
#include <vector>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;
using namespace solidity::yul::ssa::transform;

void transform::cleanUnreachableBlocks(SSACFG& _cfg)
{
	std::vector<std::uint8_t> reachable(_cfg.numBlocks(), false);
	std::deque worklist{_cfg.entry};
	reachable[_cfg.entry.value] = true;
	while (!worklist.empty())
	{
		auto const blockId = worklist.front();
		worklist.pop_front();
		auto const& block = _cfg.block(blockId);
		block.forEachExit([&](BlockId const _exitBlock) {
			if (!reachable[_exitBlock.value])
			{
				reachable[_exitBlock.value] = true;
				worklist.push_back(_exitBlock);
			}
		});
	}

	auto& store = _cfg.instructionStore();
	for (SSACFG::BlockId blockId{0}; blockId.value < _cfg.numBlocks(); ++blockId.value)
	{
		auto& block = _cfg.block(blockId);
		if (reachable[blockId.value])
			std::erase_if(block.entries, [&](auto const& entry) { return !reachable[entry.value]; });
		else
		{
			for (InstId const id: block.instructions)
				store.tombstone(id);
			block = {};
		}
	}

	// Post-condition: every reference reachable from a live block resolves to a
	// non-Tombstoned slot. Catches CFG-construction bugs and any future pass that
	// fails to drop a stale cross-block reference.
	for (SSACFG::BlockId blockId{0}; blockId.value < _cfg.numBlocks(); ++blockId.value)
	{
		if (!reachable[blockId.value])
			continue;
		auto const& block = _cfg.block(blockId);
		auto const checkRef = [&](InstId const _ref) {
			yulAssert(_ref.hasValue());
			yulAssert(
				store.kindOf(_ref) != InstOpcode::Tombstone,
				"reachable IR references a tombstoned Inst"
			);
		};
		for (InstId const id: block.instructions)
		{
			auto const& inst = store.inst(id);
			yulAssert(inst.opcode != InstOpcode::Tombstone);
			for (InstId const input: inst.inputs)
				checkRef(input);
			if (inst.isUpsilon())
				checkRef(_cfg.upsilonPhi(id));
		}
		std::visit(util::GenericVisitor{
			[&](SSACFG::BasicBlock::ConditionalJump const& _c) { checkRef(_c.condition); },
			[&](SSACFG::BasicBlock::FunctionReturn const& _r) {
				for (InstId const v: _r.returnValues)
					checkRef(v);
			},
			[](SSACFG::BasicBlock::Jump const&) {},
			[](SSACFG::BasicBlock::MainExit const&) {},
			[](SSACFG::BasicBlock::Terminated const&) {}
		}, block.exit);
	}
}
