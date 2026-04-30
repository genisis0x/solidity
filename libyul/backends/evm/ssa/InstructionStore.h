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
#include <map>
#include <memory>
#include <variant>
#include <vector>

namespace solidity::yul::ssa
{

class InstructionStore
{
public:
	struct LiteralPayload { u256 value; };
	struct UpsilonPayload { InstId targetPhi; };
	struct ExtractPayload { OutputSize index; };
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

	using Payload = std::variant<
		LiteralPayload,
		UpsilonPayload,
		ExtractPayload,
		BuiltinCall,
		Call
	>;

	struct Inst
	{
		InstOpcode opcode;
		BlockId block{};
		std::vector<InstId> inputs{};
		/// Owning pointer to the per-Inst payload. Null for opcodes that carry no
		/// payload (Phi, FunctionArg, Unreachable, Identity, Nop, Tombstone).
		std::unique_ptr<Payload> payload{};

		constexpr bool isPhi() const noexcept { return opcode == InstOpcode::Phi; }
		constexpr bool isUpsilon() const noexcept { return opcode == InstOpcode::Upsilon; }
		constexpr bool isLiteral() const noexcept { return opcode == InstOpcode::Const; }
		constexpr bool isUnreachable() const noexcept { return opcode == InstOpcode::Unreachable; }
		constexpr bool isFunctionArg() const noexcept { return opcode == InstOpcode::FunctionArg; }
		constexpr bool isExtract() const noexcept { return opcode == InstOpcode::Extract; }
		constexpr bool isIdentity() const noexcept { return opcode == InstOpcode::Identity; }
		constexpr bool isNop() const noexcept { return opcode == InstOpcode::Nop; }
		constexpr bool isTombstone() const noexcept { return opcode == InstOpcode::Tombstone; }
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
	InstId upsilonPhi(InstId const _id) const { return payloadAs<InstOpcode::Upsilon, UpsilonPayload>(_id).targetPhi; }

	/// Returns the u256 payload of a Const Inst.
	u256 const& literalPayload(InstId const _id) const { return payloadAs<InstOpcode::Const, LiteralPayload>(_id).value; }

	BuiltinCall const& builtinPayload(InstId const _id) const { return payloadAs<InstOpcode::BuiltinCall, BuiltinCall>(_id); }

	Call const& callPayload(InstId const _id) const { return payloadAs<InstOpcode::Call, Call>(_id); }

	/// Returns the projection index of an Extract Inst.
	OutputSize extractIndex(InstId const _id) const { return payloadAs<InstOpcode::Extract, ExtractPayload>(_id).index; }

	/// Allocates a new Phi Inst defined in `_definingBlock`. Returns the new InstId.
	InstId appendPhi(BlockId const _definingBlock)
	{
		return appendInst(Inst{InstOpcode::Phi, _definingBlock, {}, nullptr});
	}

	/// Allocates a new FunctionArg Inst defined in `_entryBlock`. Returns the new InstId.
	InstId appendFunctionArg(BlockId const _entryBlock)
	{
		return appendInst(Inst{InstOpcode::FunctionArg, _entryBlock, {}, nullptr});
	}

	/// Allocates a new Unreachable Inst. Not pinned to any block (see class comment).
	InstId appendUnreachable()
	{
		return appendInst(Inst{InstOpcode::Unreachable, BlockId{}, {}, nullptr});
	}

	/// Allocates a new Const Inst with payload `_value`, pinned to `_entryBlock`.
	/// Literals are deduplicated by value (see class comment).
	InstId appendLiteral(BlockId const _entryBlock, u256 _value)
	{
		yulAssert(_entryBlock.hasValue());
		if (auto const it = m_literalDedup.find(_value); it != m_literalDedup.end())
			return it->second;
		InstId const id = appendInst(Inst{
			InstOpcode::Const,
			_entryBlock,
			{},
			std::make_unique<Payload>(LiteralPayload{_value})
		});
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
			_block,
			std::move(_inputs),
			std::make_unique<Payload>(std::move(_payload))
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
			_block,
			std::move(_inputs),
			std::make_unique<Payload>(std::move(_payload))
		});
	}

	/// Allocates a new Extract Inst projecting output `_index` of `_producer`. Returns the new InstId.
	InstId appendExtract(BlockId const _block, InstId const _producer, OutputSize const _index)
	{
		yulAssert(_block.hasValue());
		yulAssert(_producer.hasValue());
		return appendInst(Inst{
			InstOpcode::Extract,
			_block,
			{_producer},
			std::make_unique<Payload>(ExtractPayload{_index})
		});
	}

	/// Allocates a new Upsilon Inst targeting `_phi`, feeding `_value` from `_block`.
	InstId appendUpsilon(BlockId const _block, InstId const _value, InstId const _phi)
	{
		yulAssert(inst(_phi).isPhi());
		return appendInst(Inst{
			InstOpcode::Upsilon,
			_block,
			{_value},
			std::make_unique<Payload>(UpsilonPayload{_phi})
		});
	}

	/// Flips _target to Identity forwarding _forward
	void replaceWithIdentity(InstId const _target, InstId const _forward)
	{
		yulAssert(_target.hasValue());
		yulAssert(_forward.hasValue());
		yulAssert(_target != _forward, "Cannot replace value with itself");
		auto& i = inst(_target);
		// every Const InstId is the unique handle for its value, so flipping it to Identity would silently rebind
		// every other user of that literal to _forward; almost certainly not what the caller intended
		yulAssert(!i.isLiteral(), "replaceWithIdentity must not morph a deduped Const slot");
		i.opcode = InstOpcode::Identity;
		i.inputs.assign(1, _forward);
		i.payload.reset();
	}

	/// Flips _target to Const carrying _value
	void replaceWithConst(InstId _target, u256 _value)
	{
		yulAssert(_target.hasValue());
		auto& i = inst(_target);
		// No-op if already this value.
		if (i.opcode == InstOpcode::Const && std::get<LiteralPayload>(*i.payload).value == _value)
			return;
		dropDedupIfConst(i);
		if (auto const it = m_literalDedup.find(_value); it != m_literalDedup.end())
		{
			i.opcode = InstOpcode::Identity;
			i.inputs.assign(1, it->second);
			i.payload.reset();
			return;
		}
		i.opcode = InstOpcode::Const;
		i.inputs.clear();
		i.payload = std::make_unique<Payload>(LiteralPayload{_value});
		m_literalDedup.emplace(std::move(_value), _target);
	}

	/// Flips _target to Nop - scheduled . Caller must ensure _target produces no observable value (e.g., an Upsilon, or a void Call).
	void replaceWithNop(InstId _target)
	{
		yulAssert(_target.hasValue());
		auto& i = inst(_target);
		dropDedupIfConst(i);
		i.opcode = InstOpcode::Nop;
		i.inputs.clear();
		i.payload.reset();
	}

	/// Tombstones the slot at `_id
	void tombstone(InstId const _id)
	{
		yulAssert(_id.hasValue());
		auto& i = inst(_id);
		dropDedupIfConst(i);
		i.opcode = InstOpcode::Tombstone;
		i.inputs.clear();
		i.payload.reset();
		i.block = BlockId{};
		m_freeInsts.push_back(_id.value);
	}

	std::vector<std::uint32_t> const& freeInsts() const { return m_freeInsts; }

private:
	InstId appendInst(Inst _inst)
	{
		if (!m_freeInsts.empty())
		{
			std::uint32_t const idx = m_freeInsts.back();
			m_freeInsts.pop_back();
			yulAssert(m_insts[idx].opcode == InstOpcode::Tombstone);
			m_insts[idx] = std::move(_inst);
			return InstId{idx};
		}
		// max itself is reserved for the 'empty' inst
		yulAssert(m_insts.size() < std::numeric_limits<InstId::ValueType>::max());
		InstId const id{static_cast<InstId::ValueType>(m_insts.size())};
		m_insts.emplace_back(std::move(_inst));
		return id;
	}

	/// Asserts that the Inst at `_id` has opcode `Expected`, then returns its payload as `T`.
	template<InstOpcode Expected, typename T>
	T const& payloadAs(InstId const _id) const
	{
		auto const& i = inst(_id);
		yulAssert(i.opcode == Expected);
		yulAssert(i.payload);
		return std::get<T>(*i.payload);
	}

	/// If `_inst` is a Const, removes its entry from the literal dedup table. Used by
	/// the replaceWith* primitives and tombstone() before overwriting the slot.
	void dropDedupIfConst(Inst const& _inst)
	{
		if (_inst.opcode != InstOpcode::Const)
			return;
		yulAssert(_inst.payload);
		u256 const& value = std::get<LiteralPayload>(*_inst.payload).value;
		m_literalDedup.erase(value);
	}

	std::vector<Inst> m_insts;
	/// Indices of tombstoned slots in `m_insts`, available for reuse by appendInst.
	std::vector<std::uint32_t> m_freeInsts;
	std::map<u256, InstId> m_literalDedup;
};

}
