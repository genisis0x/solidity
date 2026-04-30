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
#include <functional>
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
		EVMDialect const& _evmVersion,
		std::unique_ptr<DebugInfo> _debugInfo = nullptr
	):
		evmDialect(_evmVersion),
		debugInfo(std::move(_debugInfo))
	{}

	SSACFG(SSACFG const&) = delete;
	SSACFG(SSACFG&&) = delete;
	SSACFG& operator=(SSACFG const&) = delete;
	SSACFG& operator=(SSACFG&&) = delete;
	~SSACFG() = default;

	using BlockId = ssa::BlockId;

	using BuiltinCall = InstructionStore::BuiltinCall;
	using Call = InstructionStore::Call;
	using Inst = InstructionStore::Inst;

	struct BasicBlock
	{
		struct MainExit {};
		struct ConditionalJump
		{
			InstId condition;
			BlockId nonZero;
			BlockId zero;
		};
		struct Jump
		{
			BlockId target;
		};
		struct FunctionReturn
		{
			std::vector<InstId> returnValues;
		};
		struct Terminated {};
		std::vector<BlockId> entries;
		std::vector<InstId> instructions;
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
		m_blocks.emplace_back(BasicBlock{{}, {}, BasicBlock::Terminated{}});
		if (debugInfo)
			debugInfo->setBlockDebugData(blockId, std::move(_debugData));
		return blockId;
	}
	BasicBlock& block(BlockId _id) { return m_blocks.at(_id.value); }
	BasicBlock const& block(BlockId _id) const { return m_blocks.at(_id.value); }
	size_t numBlocks() const { return m_blocks.size(); }

	InstructionStore& instructionStore() { return m_instructions; }
	InstructionStore const& instructionStore() const { return m_instructions; }

	Inst& inst(InstId _id) { return m_instructions.inst(_id); }
	Inst const& inst(InstId _id) const { return m_instructions.inst(_id); }
	size_t numInsts() const { return m_instructions.numInsts(); }
	std::vector<Inst> const& instructions() const { return m_instructions.instructions(); }

	/// Returns the opcode category for a given InstId.
	InstOpcode kindOf(InstId const _id) const { return m_instructions.kindOf(_id); }

	bool isPhi(InstId const _id) const { return inst(_id).isPhi(); }
	bool isUpsilon(InstId const _id) const { return inst(_id).isUpsilon(); }
	bool isLiteral(InstId const _id) const { return inst(_id).isLiteral(); }
	bool isUnreachable(InstId const _id) const { return inst(_id).isUnreachable(); }
	bool isFunctionArg(InstId const _id) const { return inst(_id).isFunctionArg(); }
	bool isExtract(InstId const _id) const { return inst(_id).isExtract(); }
	bool isOperation(InstId const _id) const { return inst(_id).isOperation(); }

	/// Returns the phi targeted by an Upsilon Inst.
	InstId upsilonPhi(InstId const _id) const { return m_instructions.upsilonPhi(_id); }

	/// Returns the u256 payload of a Const Inst.
	u256 const& literalPayload(InstId const _id) const { return m_instructions.literalPayload(_id); }

	BuiltinCall const& builtinPayload(InstId const _id) const { return m_instructions.builtinPayload(_id); }

	Call const& callPayload(InstId const _id) const { return m_instructions.callPayload(_id); }

	/// Returns the projection index of an Extract Inst.
	OutputSize extractIndex(InstId const _id) const { return m_instructions.extractIndex(_id); }

	/// Creates a Phi Inst in the given block and returns its InstId.
	InstId newPhi(BlockId const _definingBlock)
	{
		InstId const id = scheduleInBlock(m_instructions.appendPhi(_definingBlock), _definingBlock);
		if (debugInfo)
			debugInfo->setValueDebugData(id, debugInfo->blockDebugData(_definingBlock));
		return id;
	}

	InstId newFunctionArgument()
	{
		InstId const id = scheduleInBlock(m_instructions.appendFunctionArg(entry), entry);
		if (debugInfo)
			debugInfo->setValueDebugData(id, debugInfo->blockDebugData(entry));
		return id;
	}

	InstId unreachableValue()
	{
		return m_instructions.appendUnreachable();
	}

	/// Literal InstIds are deduplicated. Const Insts are pinned to the entry block.
	InstId newLiteral(langutil::DebugData::ConstPtr _debugData, u256 _value)
	{
		auto const beforeCount = m_instructions.numInsts();
		InstId const id = m_instructions.appendLiteral(entry, std::move(_value));
		// Newly allocated (not deduplicated): schedule in entry and attach debug data.
		if (m_instructions.numInsts() > beforeCount)
		{
			scheduleInBlock(id, entry);
			if (debugInfo)
				debugInfo->setValueDebugData(id, std::move(_debugData));
		}
		return id;
	}

	InstId makeBuiltinCall(
		BlockId const _block,
		BuiltinCall _payload,
		std::vector<InstId> _inputs,
		langutil::DebugData::ConstPtr _debugData = {}
	)
	{
		InstId const id = scheduleInBlock(
			m_instructions.appendBuiltinCall(_block, std::move(_payload), std::move(_inputs)),
			_block
		);
		if (debugInfo && _debugData)
			debugInfo->setInstDebugData(id, std::move(_debugData));
		return id;
	}

	InstId makeCall(
		BlockId const _block,
		Call _payload,
		std::vector<InstId> _inputs,
		langutil::DebugData::ConstPtr _debugData = {}
	)
	{
		InstId const id = scheduleInBlock(
			m_instructions.appendCall(_block, std::move(_payload), std::move(_inputs)),
			_block
		);
		if (debugInfo && _debugData)
			debugInfo->setInstDebugData(id, std::move(_debugData));
		return id;
	}

	/// Creates an Extract projecting output `_index` of `_producer` and schedules it in `_block`.
	InstId makeExtract(
		BlockId const _block,
		InstId const _producer,
		OutputSize const _index,
		langutil::DebugData::ConstPtr _debugData = {}
	)
	{
		yulAssert(_index < maxOutputs);
		InstId const id = scheduleInBlock(
			m_instructions.appendExtract(_block, _producer, _index),
			_block
		);
		if (debugInfo && _debugData)
			debugInfo->setValueDebugData(id, std::move(_debugData));
		return id;
	}

	InstId emitUpsilon(BlockId const _block, InstId const _value, InstId const _phi)
	{
		return scheduleInBlock(m_instructions.appendUpsilon(_block, _value, _phi), _block);
	}

	std::string toDot(
		bool _includeDiGraphDefinition=true,
		std::optional<size_t> _functionIndex=std::nullopt,
		LivenessAnalysis const* _liveness=nullptr,
		ControlFlowGraphs const* _controlFlow=nullptr
	) const;

private:
	InstId scheduleInBlock(InstId const _id, BlockId const _block)
	{
		m_blocks.at(_block.value).instructions.push_back(_id);
		return _id;
	}

	std::vector<BasicBlock> m_blocks;
	InstructionStore m_instructions;
public:
	EVMDialect const& evmDialect;
	std::unique_ptr<DebugInfo> debugInfo;
	BlockId entry = BlockId{0};
	std::set<BlockId> exits;
	std::string name{};
	bool canContinue = true;
	std::vector<InstId> arguments;
	std::size_t numReturns = 0;

	bool isMainGraph() const { return name.empty(); }

	/// Iterates `_block.instructions`, invoking `_fn(instId, inst)` for every Phi.
	template<typename Callable>
	void forEachPhi(BasicBlock const& _block, Callable&& _fn)
	{
		forEachInstWhere(*this, _block, &Inst::isPhi, std::forward<Callable>(_fn));
	}
	template<typename Callable>
	void forEachPhi(BasicBlock const& _block, Callable&& _fn) const
	{
		forEachInstWhere(*this, _block, &Inst::isPhi, std::forward<Callable>(_fn));
	}

	/// Iterates `_block.instructions`, invoking `_fn(instId, inst)` for every Upsilon.
	template<typename Callable>
	void forEachUpsilon(BasicBlock const& _block, Callable&& _fn)
	{
		forEachInstWhere(*this, _block, &Inst::isUpsilon, std::forward<Callable>(_fn));
	}
	template<typename Callable>
	void forEachUpsilon(BasicBlock const& _block, Callable&& _fn) const
	{
		forEachInstWhere(*this, _block, &Inst::isUpsilon, std::forward<Callable>(_fn));
	}

	/// Iterates `_block.instructions`, invoking `_fn(instId, inst)` for every operation
	/// (Call or BuiltinCall).
	template<typename Callable>
	void forEachOperation(BasicBlock const& _block, Callable&& _fn)
	{
		forEachInstWhere(*this, _block, &Inst::isOperation, std::forward<Callable>(_fn));
	}
	template<typename Callable>
	void forEachOperation(BasicBlock const& _block, Callable&& _fn) const
	{
		forEachInstWhere(*this, _block, &Inst::isOperation, std::forward<Callable>(_fn));
	}

	/// Returns the contiguous range of Extract InstIds that immediately follow the
	/// operation at index `_opPos` in `_block.instructions`. Returns an empty vector when
	/// the operation has no Extracts (single- or zero-output cases).
	std::vector<InstId> extractsOf(BasicBlock const& _block, std::size_t _opPos) const
	{
		std::vector<InstId> result;
		for (std::size_t i = _opPos + 1; i < _block.instructions.size(); ++i)
		{
			InstId const id = _block.instructions[i];
			if (!inst(id).isExtract())
				break;
			yulAssert(inst(id).inputs.size() == 1);
			if (inst(id).inputs.at(0) != _block.instructions[_opPos])
				break;
			result.push_back(id);
		}
		return result;
	}

	/// Convenience overload: locates `_op` inside `_block.instructions` and returns its
	/// trailing Extracts. Asserts that the operation is in the block.
	std::vector<InstId> extractsOf(BasicBlock const& _block, InstId const _op) const
	{
		auto const it = std::find(_block.instructions.begin(), _block.instructions.end(), _op);
		yulAssert(it != _block.instructions.end());
		return extractsOf(_block, static_cast<std::size_t>(it - _block.instructions.begin()));
	}

	/// Returns the number of return values an op produces. 0 for void ops; 1 if its
	/// own InstId is the value handle; N>1 for multi-output (in which case the op has
	/// N trailing Extracts). Looked up via the dialect (BuiltinCall) or call payload (Call).
	std::size_t numReturnsOf(InstId _op) const
	{
		auto const& i = inst(_op);
		switch (i.opcode)
		{
		case InstOpcode::Call:
			return callPayload(_op).numReturns;
		case InstOpcode::BuiltinCall:
			return evmDialect.builtin(builtinPayload(_op).builtin).numReturns;
		default:
			yulAssert(false, "numReturnsOf: not an operation");
		}
	}

	/// Logical value-handles produced by an operation, in stack order (bottom to top of
	/// the slots it pushes). Empty for 0-output ops; {op} for single-output; trailing
	/// Extracts for multi-output. Asserts that the op's structural position matches its
	/// numReturns.
	std::vector<InstId> opOutputs(BasicBlock const& _block, std::size_t _opPos) const
	{
		InstId const op = _block.instructions[_opPos];
		std::size_t const n = numReturnsOf(op);
		if (n == 0)
			return {};
		if (n == 1)
			return {op};
		auto extracts = extractsOf(_block, _opPos);
		yulAssert(extracts.size() == n, "Multi-output op missing trailing Extracts");
		return extracts;
	}

	/// InstId overload: locates `_op` inside `_block.instructions` and returns its
	/// logical outputs.
	std::vector<InstId> opOutputs(BasicBlock const& _block, InstId const _op) const
	{
		auto const it = std::find(_block.instructions.begin(), _block.instructions.end(), _op);
		yulAssert(it != _block.instructions.end());
		return opOutputs(_block, static_cast<std::size_t>(it - _block.instructions.begin()));
	}

private:
	/// Shared implementation for the public `forEach*` overloads; works for both
	/// `SSACFG&` and `SSACFG const&` since `_self.inst(id)` propagates const-ness.
	template<typename Self, typename Pred, typename Callable>
	static void forEachInstWhere(Self& _self, BasicBlock const& _block, Pred _pred, Callable&& _fn)
	{
		for (InstId const id: _block.instructions)
		{
			auto& inst = _self.inst(id);
			if (std::invoke(_pred, inst))
				_fn(id, inst);
		}
	}
};

}
