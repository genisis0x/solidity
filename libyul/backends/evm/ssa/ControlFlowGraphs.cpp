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

#include <libyul/backends/evm/ssa/ControlFlowGraphs.h>

#include <libyul/backends/evm/EVMDialect.h>

#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

ControlFlowGraphsLiveness::ControlFlowGraphsLiveness(ControlFlowGraphs const& _controlFlow):
	controlFlowGraphs(_controlFlow),
	cfgLiveness(_controlFlow.functionGraphs | ranges::views::transform([](auto const& _cfg) { return std::make_unique<LivenessAnalysis>(*_cfg); }) | ranges::to<std::vector>)
{ }

std::string ControlFlowGraphsLiveness::toDot() const
{
	return controlFlowGraphs.get().toDot(this);
}

std::optional<u256> ControlFlow::memoryGuard() const
{
	if (functionGraphs.empty())
		return std::nullopt;

	if (m_memoryGuardCache)
		return *m_memoryGuardCache;

	EVMDialect const& dialect = functionGraphs.front()->evmDialect;
	BuiltinFunction const& memoryGuardBuiltin = dialect.builtin(*dialect.findBuiltin("memoryguard"));

	std::optional<u256> offset;
	for (auto const& graph: functionGraphs)
		for (SSACFG::BlockId::ValueType blockIndex = 0; blockIndex < graph->numBlocks(); ++blockIndex)
			for (SSACFG::OperationId const operationId: graph->block(SSACFG::BlockId{blockIndex}).operations)
			{
				auto const& op = graph->operation(operationId);
				auto const* builtinCall = std::get_if<SSACFG::BuiltinCall>(&op.kind);
				if (!builtinCall || &builtinCall->builtin.get() != &memoryGuardBuiltin)
					continue;

				FunctionCall const& astCall = builtinCall->call.get();
				yulAssert(astCall.arguments.size() == 1);
				Literal const* literal = std::get_if<Literal>(&astCall.arguments.front());
				yulAssert(literal && literal->kind == LiteralKind::Number);
				u256 const argument = literal->value.value();

				if (!offset)
					offset = argument;
				else
					yulAssert(*offset == argument, "Inconsistent memory guard args found.");
			}

	m_memoryGuardCache = offset;
	return *m_memoryGuardCache;
}
