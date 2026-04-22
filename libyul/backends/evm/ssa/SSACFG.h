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
 * Control flow graph and stack layout structures used during code generation.
 */

#pragma once

#include <libyul/backends/evm/ssa/InstructionStore.h>
#include <libyul/backends/evm/ssa/SSACFGDebugInfo.h>
#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <libyul/backends/evm/EVMDialect.h>

#include <libyul/AST.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/Dialect.h>
#include <libyul/Exceptions.h>

#include <libsolutil/Numeric.h>

#include <concepts>
#include <string>
#include <vector>

namespace solidity::yul::ssa
{
class LivenessAnalysis;
struct ControlFlowGraphs;

class SSACFG
{
public:
	using DebugInfo = SSACFGDebugInfo;

	explicit SSACFG(
		EVMDialect const& _evmDialect,
		std::unique_ptr<DebugInfo> _debugInfo = nullptr
	):
		evmDialect(_evmDialect),
		debugInfo(std::move(_debugInfo))
	{
		// memoryguard is only available for yul objects, not for raw inline assembly. but we never want to
		// use inline assembly dialect for SSA-CFG
		yulAssert(
			_evmDialect.findBuiltin("memoryguard").has_value(),
			"SSA-CFG codegen is only valid over dialects that possess memoryguard as builtin."
		);
	}

	SSACFG(SSACFG const&) = delete;
	SSACFG(SSACFG&&) = delete;
	SSACFG& operator=(SSACFG const&) = delete;
	SSACFG& operator=(SSACFG&&) = delete;
	~SSACFG() = default;

	using BlockId = ssa::BlockId;
	using InstId = ssa::InstId;
	using ValueId = ssa::ValueId;

	using BuiltinCall = InstructionStore::BuiltinCall;
	using Call = InstructionStore::Call;
	using Operation = InstructionStore::Operation;

	/// Upsilon records a phi pre-image at a block exit.
	/// Upsilon(value, phi) means: the value flowing into `phi` from this block is `value`.
	/// Lives in the predecessor block; the corresponding Phi lives in the successor.
	struct Upsilon
	{
		ValueId value;  ///< pre-image value for the phi
		ValueId phi;    ///< target phi
	};

	struct BasicBlock
	{
		struct MainExit {};
		struct ConditionalJump
		{
			ValueId condition;
			BlockId nonZero;
			BlockId zero;
		};
		struct Jump
		{
			BlockId target;
		};
		struct FunctionReturn
		{
			std::vector<ValueId> returnValues;
		};
		struct Terminated {};
		std::vector<BlockId> entries;
		std::vector<ValueId> phis;
		std::vector<InstId> operations;
		/// Upsilon assignments placed at the block exit (before the terminator).
		/// They record the phi pre-images for successor blocks.
		std::vector<Upsilon> upsilons;
		std::variant<MainExit, Jump, ConditionalJump, FunctionReturn, Terminated> exit = MainExit{};

		template<std::invocable<BlockId> Callable>
		void forEachExit(Callable&& _callable) const
		{
			if (auto* jump = std::get_if<Jump>(&exit))
				_callable(jump->target);
			else if (auto* conditionalJump = std::get_if<ConditionalJump>(&exit))
			{
				_callable(conditionalJump->nonZero);
				_callable(conditionalJump->zero);
			}
		}

		bool isMainExitBlock() const { return std::holds_alternative<MainExit>(exit); }
		bool isTerminationBlock() const { return std::holds_alternative<Terminated>(exit); }
		bool isFunctionReturnBlock() const { return std::holds_alternative<FunctionReturn>(exit); }
		bool isJumpBlock() const { return std::holds_alternative<Jump>(exit); }
	};

	BlockId makeBlock(langutil::DebugData::ConstPtr _debugData)
	{
		// max itself is reserved for the 'empty' block
		yulAssert(m_blocks.size() < std::numeric_limits<BlockId::ValueType>::max());
		BlockId const blockId{static_cast<BlockId::ValueType>(m_blocks.size())};
		m_blocks.emplace_back(BasicBlock{{}, {}, {}, {}, BasicBlock::Terminated{}});
		if (debugInfo)
			debugInfo->setBlockDebugData(blockId, std::move(_debugData));
		return blockId;
	}
	BasicBlock& block(BlockId _id) { return m_blocks.at(_id.value); }
	BasicBlock const& block(BlockId _id) const { return m_blocks.at(_id.value); }
	size_t numBlocks() const { return m_blocks.size(); }

	InstructionStore& instructionStore() { return m_instructions; }
	InstructionStore const& instructionStore() const { return m_instructions; }

	Operation& operation(InstId _id) { return m_instructions.operation(_id); }
	Operation const& operation(InstId _id) const { return m_instructions.operation(_id); }

	InstructionStore::LiteralValue const& literalInfo(ValueId const& _valueId) const { return m_instructions.literalInfo(_valueId); }
	InstructionStore::PhiValue const& phiInfo(ValueId const& _valueId) const { return m_instructions.phiInfo(_valueId); }
	InstructionStore::PhiValue& phiInfo(ValueId const& _valueId) { return m_instructions.phiInfo(_valueId); }
	InstructionStore::VariableValue const& variableInfo(ValueId const& _valueId) const { return m_instructions.variableInfo(_valueId); }

	ValueId newPhi(BlockId const _definingBlock)
	{
		auto const id = m_instructions.newPhi(_definingBlock);
		if (debugInfo)
			debugInfo->setValueDebugData(id, debugInfo->blockDebugData(_definingBlock));
		return id;
	}

	ValueId newVariable(BlockId const _definingBlock)
	{
		auto const id = m_instructions.newVariable(_definingBlock);
		if (debugInfo)
			debugInfo->setValueDebugData(id, debugInfo->blockDebugData(_definingBlock));
		return id;
	}

	ValueId unreachableValue() { return m_instructions.unreachableValue(); }

	ValueId newLiteral(langutil::DebugData::ConstPtr _debugData, u256 _value)
	{
		auto const [id, allocated] = m_instructions.newLiteral(std::move(_value));
		if (debugInfo && allocated)
			debugInfo->setValueDebugData(id, std::move(_debugData));
		return id;
	}

	InstId makeOperation(BlockId _block, Operation _op, langutil::DebugData::ConstPtr _debugData = {})
	{
		auto const id = m_instructions.makeOperation(_block, std::move(_op));
		if (debugInfo)
			debugInfo->setOperationDebugData(id, std::move(_debugData));
		return id;
	}

	std::string toDot(
		bool _includeDiGraphDefinition=true,
		std::optional<size_t> _functionIndex=std::nullopt,
		LivenessAnalysis const* _liveness=nullptr,
		ControlFlowGraphs const* _controlFlow=nullptr
	) const;

private:
	std::vector<BasicBlock> m_blocks;
	InstructionStore m_instructions;
public:
	EVMDialect const& evmDialect;
	std::unique_ptr<DebugInfo> debugInfo;
	BlockId entry = BlockId{0};
	std::set<BlockId> exits;
	std::string name{};
	bool canContinue = true;
	std::vector<ValueId> arguments;
	std::size_t numReturns = 0;

	bool isMainGraph() const { return name.empty(); }
};

}
