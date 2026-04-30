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

#include <libyul/backends/evm/ssa/transform/IdentityRemover.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libsolutil/Visitor.h>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{

/// Path-compresses Identity chains starting at `_id`. Returns the terminal
/// (non-Identity) target. Self-loops in the chain are forbidden by the
/// replaceWith* contract.
InstId resolve(SSACFG const& _cfg, InstId _id)
{
	while (_id.hasValue() && _cfg.kindOf(_id) == InstOpcode::Identity)
	{
		auto const& inst = _cfg.inst(_id);
		yulAssert(inst.inputs.size() == 1);
		yulAssert(inst.inputs[0] != _id, "Identity self-loop");
		_id = inst.inputs[0];
	}
	return _id;
}

void rewriteInputs(SSACFG const& _cfg, std::vector<InstId>& _inputs)
{
	for (auto& input: _inputs)
		input = resolve(_cfg, input);
}

}

void transform::removeIdentities(SSACFG& _cfg)
{
	// rewrite every Identity reference reachable from the IR to its terminal target
	auto& store = _cfg.instructionStore();
	for (std::uint32_t i = 0; i < store.numInsts(); ++i)
	{
		InstId const id{i};
		auto& inst = _cfg.inst(id);
		switch (inst.opcode)
		{
		case InstOpcode::Identity:
		case InstOpcode::Nop:
		case InstOpcode::Tombstone:
		case InstOpcode::Const:
		case InstOpcode::Phi:
		case InstOpcode::FunctionArg:
		case InstOpcode::Unreachable:
			// Identity / Nop / Tombstone: rewriting their inputs is pointless as they will be tombstoned
			// Const / Phi / FunctionArg / Unreachable: can't have identity inputs by construction
			break;
		case InstOpcode::Upsilon:
		{
			yulAssert(inst.inputs.size() == 1);
			inst.inputs[0] = resolve(_cfg, inst.inputs[0]);
			// The Upsilon's targetPhi never becomes Identity: the trivial-phi pass
			// turns dead upsilons into Nops before flipping the phi to Identity, so by
			// the time we observe a live Upsilon its phi is still a real Phi.
			yulAssert(_cfg.kindOf(_cfg.upsilonPhi(id)) == InstOpcode::Phi);
			break;
		}
		case InstOpcode::Extract:
		case InstOpcode::BuiltinCall:
		case InstOpcode::Call:
			rewriteInputs(_cfg, inst.inputs);
			break;
		}
	}

	// Rewrite block-exit references that name InstIds outside `inst.inputs`.
	for (BlockId blockId{0}; blockId.value < _cfg.numBlocks(); ++blockId.value)
		std::visit(util::GenericVisitor{
			[&](SSACFG::BasicBlock::ConditionalJump& c) { c.condition = resolve(_cfg, c.condition); },
			[&](SSACFG::BasicBlock::FunctionReturn& r) { rewriteInputs(_cfg, r.returnValues); },
			[](SSACFG::BasicBlock::Jump&) {},
			[](SSACFG::BasicBlock::MainExit&) {},
			[](SSACFG::BasicBlock::Terminated&) {}
		}, _cfg.block(blockId).exit);

	// Drop Identity / Nop entries from each block and tombstone their slots. No live reference reads through any of
	// these slots, so reusing them is safe.
	for (BlockId blockId{0}; blockId.value < _cfg.numBlocks(); ++blockId.value)
	{
		auto& block = _cfg.block(blockId);
		std::vector<InstId> toTombstone;
		std::erase_if(block.instructions, [&](InstId const _id) {
			auto const op = _cfg.kindOf(_id);
			if (op == InstOpcode::Identity || op == InstOpcode::Nop)
			{
				toTombstone.push_back(_id);
				return true;
			}
			return false;
		});
		for (InstId const id: toTombstone)
			store.tombstone(id);
	}
}
