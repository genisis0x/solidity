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
 * Owns the instruction table of an SSACFG: hands out InstIds, stores opcode
 * payloads, and provides lookups. Has no knowledge of basic blocks or debug
 * information; SSACFG composes those concerns on top.
 */

#pragma once

#include <libyul/backends/evm/ssa/SSACFGTypes.h>

#include <libyul/AST.h>
#include <libyul/Builtins.h>
#include <libyul/Exceptions.h>

#include <libsolutil/Numeric.h>

#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace solidity::yul::ssa
{

class InstructionStore
{
public:
	/// Sentinel value for `Inst::payloadIndex` when the opcode has no side-table payload.
	static constexpr std::uint32_t NoPayload = std::numeric_limits<std::uint32_t>::max();

	struct BuiltinCall
	{
		BuiltinHandle builtin;
		/// Literal-kind arguments
		std::vector<Literal> literalArguments;
	};
	struct Call
	{
		FunctionGraphID graphID;
		bool canContinue;
		/// Cached return count of the callee. The Call inst itself produces no logical
		/// output; its return slots are named by the trailing Extract insts (multi-output)
		/// or by the Call's own InstId (single-output). Used by codegen to know how many
		/// stack slots the call leaves behind.
		std::size_t numReturns;
	};
	struct Inst
	{
		InstOpcode opcode;
		std::uint32_t payloadIndex = NoPayload;
		BlockId block{};
		std::vector<InstId> inputs{};

		constexpr bool isPhi() const noexcept { return opcode == InstOpcode::Phi; }
		constexpr bool isUpsilon() const noexcept { return opcode == InstOpcode::Upsilon; }
		constexpr bool isLiteral() const noexcept { return opcode == InstOpcode::Const; }
		constexpr bool isUnreachable() const noexcept { return opcode == InstOpcode::Unreachable; }
		constexpr bool isFunctionArg() const noexcept { return opcode == InstOpcode::FunctionArg; }
		constexpr bool isExtract() const noexcept { return opcode == InstOpcode::Extract; }
		/// Operation = a Call or BuiltinCall.
		constexpr bool isOperation() const noexcept
		{
			return opcode == InstOpcode::Call || opcode == InstOpcode::BuiltinCall;
		}
	};

	InstructionStore() = default;
	InstructionStore(InstructionStore const&) = delete;
	InstructionStore(InstructionStore&&) = default;
	InstructionStore& operator=(InstructionStore const&) = delete;
	InstructionStore& operator=(InstructionStore&&) = default;
	~InstructionStore() = default;

	Inst& inst(InstId _id) { return m_insts.at(_id.value); }
	Inst const& inst(InstId _id) const { return m_insts.at(_id.value); }
	std::size_t numInsts() const { return m_insts.size(); }
	std::vector<Inst> const& instructions() const { return m_insts; }

	/// Returns the opcode category for a given InstId.
	InstOpcode kindOf(InstId const _id) const { return inst(_id).opcode; }

	/// Returns the phi targeted by an Upsilon Inst.
	InstId upsilonPhi(InstId const _id) const { return payloadAs<InstOpcode::Upsilon>(_id, m_upsilonPhis); }

	/// Returns the u256 payload of a Const Inst.
	u256 const& literalPayload(InstId const _id) const { return payloadAs<InstOpcode::Const>(_id, m_literalPayloads); }

	BuiltinCall const& builtinPayload(InstId const _id) const { return payloadAs<InstOpcode::BuiltinCall>(_id, m_builtinPayloads); }

	Call const& callPayload(InstId const _id) const { return payloadAs<InstOpcode::Call>(_id, m_callPayloads); }

	/// Returns the projection index of an Extract Inst.
	OutputSize extractIndex(InstId const _id) const { return payloadAs<InstOpcode::Extract>(_id, m_extractIndices); }

	/// Allocates a new Phi Inst defined in `_definingBlock`. Returns the new InstId.
	InstId appendPhi(BlockId const _definingBlock)
	{
		return appendInst(Inst{InstOpcode::Phi, NoPayload, _definingBlock, {}});
	}

	/// Allocates a new FunctionArg Inst defined in `_entryBlock`. Returns the new InstId.
	InstId appendFunctionArg(BlockId const _entryBlock)
	{
		return appendInst(Inst{InstOpcode::FunctionArg, NoPayload, _entryBlock, {}});
	}

	/// Allocates a new Unreachable Inst. Not pinned to any block (see class comment).
	InstId appendUnreachable()
	{
		return appendInst(Inst{InstOpcode::Unreachable, NoPayload, BlockId{}, {}});
	}

	/// Allocates a new Const Inst with payload `_value`, pinned to `_entryBlock`.
	/// Literals are deduplicated by value (see class comment).
	InstId appendLiteral(BlockId const _entryBlock, u256 _value)
	{
		yulAssert(_entryBlock.hasValue());
		if (auto const it = m_literalDedup.find(_value); it != m_literalDedup.end())
			return it->second;
		auto const payloadIdx = allocPayload(m_literalPayloads, _value);
		InstId const id = appendInst(Inst{InstOpcode::Const, payloadIdx, _entryBlock, {}});
		m_literalDedup.emplace(std::move(_value), id);
		return id;
	}

	/// Allocates a new BuiltinCall Inst. Returns the new InstId.
	InstId appendBuiltinCall(
		BlockId const _block,
		BuiltinCall _payload,
		std::vector<InstId> _inputs
	)
	{
		yulAssert(_block.hasValue());
		return appendInst(Inst{
			InstOpcode::BuiltinCall,
			allocPayload(m_builtinPayloads, std::move(_payload)),
			_block,
			std::move(_inputs)
		});
	}

	/// Allocates a new Call Inst. Returns the new InstId.
	InstId appendCall(
		BlockId const _block,
		Call _payload,
		std::vector<InstId> _inputs
	)
	{
		yulAssert(_block.hasValue());
		return appendInst(Inst{
			InstOpcode::Call,
			allocPayload(m_callPayloads, std::move(_payload)),
			_block,
			std::move(_inputs)
		});
	}

	/// Allocates a new Extract Inst projecting output `_index` of `_producer`. Returns the new InstId.
	InstId appendExtract(BlockId const _block, InstId const _producer, OutputSize const _index)
	{
		yulAssert(_block.hasValue());
		yulAssert(_producer.hasValue());
		return appendInst(Inst{
			InstOpcode::Extract,
			allocPayload(m_extractIndices, _index),
			_block,
			{_producer}
		});
	}

	/// Allocates a new Upsilon Inst targeting `_phi`, feeding `_value` from `_block`.
	InstId appendUpsilon(BlockId const _block, InstId const _value, InstId const _phi)
	{
		yulAssert(inst(_phi).isPhi());
		return appendInst(Inst{InstOpcode::Upsilon, allocPayload(m_upsilonPhis, _phi), _block, {_value}});
	}

private:
	InstId appendInst(Inst _inst)
	{
		// max itself is reserved for the 'empty' inst
		yulAssert(m_insts.size() < std::numeric_limits<InstId::ValueType>::max());
		InstId const id{static_cast<InstId::ValueType>(m_insts.size())};
		m_insts.emplace_back(std::move(_inst));
		return id;
	}

	/// Asserts that the Inst at `_id` has opcode `Expected`, then returns its payload from `_store`.
	template<InstOpcode Expected, typename Store>
	typename Store::value_type const& payloadAs(InstId const _id, Store const& _store) const
	{
		auto const& i = inst(_id);
		yulAssert(i.opcode == Expected);
		return _store.at(i.payloadIndex);
	}

	/// Pushes `_payload` into `_store` and returns the resulting payload index. Asserts
	/// the index fits in payloadIndex's u32 representation (max is reserved for NoPayload).
	template<typename T>
	std::uint32_t allocPayload(std::vector<T>& _store, T _payload)
	{
		yulAssert(_store.size() < NoPayload);
		auto const idx = static_cast<std::uint32_t>(_store.size());
		_store.push_back(std::move(_payload));
		return idx;
	}

	std::vector<Inst> m_insts;
	std::vector<u256> m_literalPayloads;
	std::map<u256, InstId> m_literalDedup;
	std::vector<InstId> m_upsilonPhis;
	std::vector<BuiltinCall> m_builtinPayloads;
	std::vector<Call> m_callPayloads;
	std::vector<OutputSize> m_extractIndices;
};

}
