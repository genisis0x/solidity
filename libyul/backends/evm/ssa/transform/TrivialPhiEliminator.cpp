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

#include <libyul/backends/evm/ssa/transform/TrivialPhiEliminator.h>

#include <libyul/backends/evm/ssa/SSACFG.h>

#include <libsolutil/Visitor.h>

#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/algorithm/count_if.hpp>
#include <range/v3/algorithm/replace.hpp>

#include <algorithm>
#include <vector>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{

struct TrivialPhiEliminator
{
	SSACFG& cfg;

	// phi_id -> values of upsilons targeting this phi
	std::vector<std::vector<InstId>> upsilonValuesForPhi;
	// phi_id -> blocks containing upsilons targeting this phi
	std::vector<std::vector<SSACFG::BlockId>> phiTargetBlocks;
	// phi_id -> phis whose upsilon values reference this phi
	std::vector<std::vector<InstId>> reversePhiUpsilonValues;
	// phi_id -> canonical replacement (default = no substitution)
	std::vector<InstId> substitution;

	explicit TrivialPhiEliminator(SSACFG& _cfg): cfg(_cfg) {}

	void run()
	{
		cleanUnreachableUpsilons();
		buildIndices();
		substituteDeadPhis();

		// run elimination repeatedly until convergence
		auto substitutionsBeforeEliminateAll = static_cast<std::size_t>(ranges::count_if(substitution, [](auto const& s) { return s.hasValue(); }));
		auto substitutionsAfterEliminateAll = substitutionsBeforeEliminateAll;
		do
		{
			substitutionsBeforeEliminateAll = substitutionsAfterEliminateAll;
			eliminateAll();
			substitutionsAfterEliminateAll = static_cast<std::size_t>(ranges::count_if(substitution, [](auto const& s) { return s.hasValue(); }));
		} while (substitutionsAfterEliminateAll > substitutionsBeforeEliminateAll);
		applySubstitutions();
	}

private:
	void cleanUnreachableUpsilons()
	{
		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			auto& block = cfg.block(blockId);
			std::erase_if(block.instructions, [this](InstId const _id) {
				auto const& inst = cfg.inst(_id);
				return inst.isUpsilon() && cfg.isUnreachable(inst.inputs.at(0));
			});
		}
	}

	void buildIndices()
	{
		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			cfg.forEachUpsilon(cfg.block(blockId), [&](InstId const instId, SSACFG::Inst const& inst) {
				InstId const phi = cfg.upsilonPhi(instId);
				InstId const value = inst.inputs.at(0);
				auto const phiIdx = phi.value;
				if (phiIdx >= upsilonValuesForPhi.size())
				{
					upsilonValuesForPhi.resize(phiIdx + 1);
					phiTargetBlocks.resize(phiIdx + 1);
				}
				if (phiIdx >= reversePhiUpsilonValues.size())
					reversePhiUpsilonValues.resize(phiIdx + 1);
				upsilonValuesForPhi[phiIdx].push_back(value);
				phiTargetBlocks[phiIdx].push_back(blockId);
				if (cfg.isPhi(value))
				{
					auto const valIdx = value.value;
					if (valIdx >= reversePhiUpsilonValues.size())
						reversePhiUpsilonValues.resize(valIdx + 1);
					reversePhiUpsilonValues[valIdx].push_back(phi);
				}
			});
		}
		substitution.resize(std::max(upsilonValuesForPhi.size(), size_t{1}));
	}

	/// Phis that have no upsilons targeting them are dead — either in unreachable blocks
	/// or orphaned from cycle-breaking.
	/// Replace references to them with unreachableValue in the upsilon value index so they don't block triviality
	/// detection.
	void substituteDeadPhis()
	{
		auto const unreachable = cfg.unreachableValue();
		for (auto& upsilonValues: upsilonValuesForPhi)
			for (auto& val: upsilonValues)
				if (cfg.isPhi(val))
				{
					auto const idx = val.value;
					bool const hasUpsilons = idx < upsilonValuesForPhi.size() && !upsilonValuesForPhi[idx].empty();
					if (!hasUpsilons)
						val = unreachable;
				}
	}

	void eliminateAll()
	{
		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			// Snapshot the phi InstIds because tryRemove mutates block.instructions.
			std::vector<InstId> phis;
			cfg.forEachPhi(cfg.block(blockId), [&](InstId const instId, SSACFG::Inst const&) {
				phis.push_back(instId);
			});
			for (auto const phi: phis)
				tryRemove(phi);
		}
	}

	void tryRemove(InstId _phi)
	{
		auto const phiIdx = _phi.value;

		// early exit if this phi was already processed
		if (phiIdx < substitution.size() && substitution[phiIdx].hasValue())
			return;

		// check triviality and canonicalize arguments to account for already-substituted phis;
		// skip self-references and unreachable values
		InstId same;
		if (phiIdx < upsilonValuesForPhi.size())
			for (auto const arg: upsilonValuesForPhi[phiIdx])
			{
				auto const resolved = canonicalize(arg);
				if (resolved == same || resolved == _phi || cfg.isUnreachable(resolved))
					continue;
				if (same.hasValue())
					return; // non-trivial
				same = resolved;
			}

		if (!same.hasValue())
			// phi has only self-references (unreachable cycle), or no upsilons at all
			same = cfg.unreachableValue();

		// remove the phi from its defining block
		{
			auto& block = cfg.block(cfg.inst(_phi).block);
			yulAssert(cfg.isPhi(_phi));
			std::erase(block.instructions, _phi);
		}

		// record the substitution
		if (phiIdx >= substitution.size())
			substitution.resize(phiIdx + 1);
		substitution[phiIdx] = same;

		// update upsilon.value fields and `upsilonValuesForPhi` for all phis whose upsilons reference `_phi` as a value
		std::vector<InstId> cascades;
		if (phiIdx < reversePhiUpsilonValues.size())
		{
			for (auto const targetPhi: reversePhiUpsilonValues[phiIdx])
			{
				// update upsilonValuesForPhi[targetPhi]: _phi with same
				auto& vals = upsilonValuesForPhi[targetPhi.value];
				ranges::replace(vals, _phi, same);
				if (targetPhi.value < phiTargetBlocks.size())
					for (auto const blockId: phiTargetBlocks[targetPhi.value])
						cfg.forEachUpsilon(cfg.block(blockId), [&](InstId const instId, SSACFG::Inst& inst) {
							if (cfg.upsilonPhi(instId) == targetPhi && inst.inputs.at(0) == _phi)
								inst.inputs[0] = same;
						});
				// maintain reverse index
				if (cfg.isPhi(same))
				{
					if (same.value >= reversePhiUpsilonValues.size())
						reversePhiUpsilonValues.resize(same.value + 1);
					reversePhiUpsilonValues[same.value].push_back(targetPhi);
				}
				cascades.push_back(targetPhi);
			}
			reversePhiUpsilonValues[phiIdx].clear();
		}

		// erase all upsilons targeting the removed phi.
		if (phiIdx < phiTargetBlocks.size())
		{
			for (auto const blockId: phiTargetBlocks[phiIdx])
			{
				auto& block = cfg.block(blockId);
				std::erase_if(block.instructions, [this, _phi](InstId _id) {
					auto const& ins = cfg.inst(_id);
					return ins.isUpsilon() && cfg.upsilonPhi(_id) == _phi;
				});
			}
			phiTargetBlocks[phiIdx].clear();
		}

		for (auto const cascadingPhi: cascades)
			tryRemove(cascadingPhi);
	}

	InstId canonicalize(InstId const _v)
	{
		auto const idx = _v.value;
		if (idx >= substitution.size())
			return _v;
		auto& sub = substitution[idx];
		if (!sub.hasValue())
			return _v;
		// path compression
		sub = canonicalize(sub);
		return sub;
	}

	void applySubstitutions()
	{
		if (!ranges::any_of(substitution, [](InstId const& _sub) { return _sub.hasValue(); }))
			return;

		for (SSACFG::BlockId blockId{0}; blockId.value < cfg.numBlocks(); ++blockId.value)
		{
			auto& block = cfg.block(blockId);
			for (InstId const instId: block.instructions)
			{
				auto& inst = cfg.inst(instId);
				if (inst.isUpsilon())
					inst.inputs[0] = canonicalize(inst.inputs[0]);
				else if (inst.isOperation())
					for (auto& input: inst.inputs)
						input = canonicalize(input);
			}
			std::visit(solidity::util::GenericVisitor{
				[&](SSACFG::BasicBlock::FunctionReturn& r) {
					for (auto& v: r.returnValues)
						v = canonicalize(v);
				},
				[&](SSACFG::BasicBlock::ConditionalJump& c) { c.condition = canonicalize(c.condition); },
				[](SSACFG::BasicBlock::Jump&) {},
				[](SSACFG::BasicBlock::MainExit&) {},
				[](SSACFG::BasicBlock::Terminated&) {}
			}, block.exit);
		}
	}
};

}

void transform::eliminateTrivialPhis(SSACFG& _cfg)
{
	TrivialPhiEliminator(_cfg).run();
}
