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

#include <algorithm>

using namespace solidity;
using namespace solidity::yul;
using namespace solidity::yul::ssa;

namespace
{
/// DFS-with-path-stack cycle finder over the call graph, mirroring the classic
/// ``CallGraphCycleFinder`` in ``libyul/optimiser/CallGraphGenerator.cpp`` but operating entirely
/// on ``FunctionGraphID``s instead of ``Scope::Function const*``.
struct CycleFinder
{
	using FunctionGraphID = ControlFlowGraphs::FunctionGraphID;

	std::vector<std::vector<FunctionGraphID>> const& callees;
	std::vector<std::uint8_t> recursive;
	std::vector<std::uint8_t> visited;
	std::vector<FunctionGraphID> currentPath{};

	explicit CycleFinder(std::vector<std::vector<FunctionGraphID>> const& _callees):
		callees(_callees),
		recursive(_callees.size(), 0),
		visited(_callees.size(), 0)
	{}

	void visit(FunctionGraphID _id)
	{
		if (visited[_id])
			return;
		if (auto it = std::find(currentPath.begin(), currentPath.end(), _id); it != currentPath.end())
			for (auto pathIt = it; pathIt != currentPath.end(); ++pathIt)
				recursive[*pathIt] = 1;
		else
		{
			currentPath.emplace_back(_id);
			for (FunctionGraphID callee: callees[_id])
				visit(callee);
			currentPath.pop_back();
			visited[_id] = 1;
		}
	}
};
}

ControlFlowGraphsLiveness::ControlFlowGraphsLiveness(ControlFlowGraphs const& _controlFlow):
	controlFlowGraphs(_controlFlow),
	cfgLiveness(_controlFlow.functionGraphs | ranges::views::transform([](auto const& _cfg) { return std::make_unique<LivenessAnalysis>(*_cfg); }) | ranges::to<std::vector>)
{ }

std::string ControlFlowGraphsLiveness::toDot() const
{
	return controlFlowGraphs.get().toDot(this);
}

ControlFlowRecursion::ControlFlowRecursion(ControlFlowGraphs const& _controlFlow):
	controlFlow(_controlFlow)
{
	using FunctionGraphID = ControlFlowGraphs::FunctionGraphID;
	auto const numGraphs = static_cast<FunctionGraphID>(_controlFlow.functionGraphs.size());

	// Adjacency: callees[callerId] = list of callee graph ids. All lookups are by graph id; we
	// never materialize a Scope::Function-keyed container.
	std::vector<std::vector<FunctionGraphID>> callees(numGraphs);
	for (FunctionGraphID callerId = 0; callerId < numGraphs; ++callerId)
	{
		SSACFG const& graph = *_controlFlow.functionGraphs[callerId];
		for (SSACFG::BlockId::ValueType blockIndex = 0; blockIndex < graph.numBlocks(); ++blockIndex)
			for (SSACFG::InstId const operationId: graph.block(SSACFG::BlockId{blockIndex}).operations)
			{
				auto const& op = graph.operation(operationId);
				auto const* call = std::get_if<SSACFG::Call>(&op.kind);
				if (!call)
					continue;
				FunctionGraphID const calleeId = call->graphID;
				yulAssert(calleeId < numGraphs, "Callee has no corresponding function graph.");
				callees[callerId].emplace_back(calleeId);
			}
	}

	CycleFinder finder{callees};
	// Visit every graph so we also cover disconnected cycles
	for (FunctionGraphID id = 0; id < numGraphs; ++id)
		finder.visit(id);
	recursive = std::move(finder.recursive);
}

std::optional<u256> ControlFlowGraphs::memoryGuard() const
{
	if (functionGraphs.empty())
		return std::nullopt;

	if (m_memoryGuardCache)
		return *m_memoryGuardCache;

	EVMDialect const& dialect = functionGraphs.front()->evmDialect;
	BuiltinHandle const memoryGuardHandle = *dialect.findBuiltin("memoryguard");

	std::optional<u256> offset;
	for (auto const& graph: functionGraphs)
		for (SSACFG::BlockId::ValueType blockIndex = 0; blockIndex < graph->numBlocks(); ++blockIndex)
			for (SSACFG::InstId const operationId: graph->block(SSACFG::BlockId{blockIndex}).operations)
			{
				auto const& op = graph->operation(operationId);
				auto const* builtinCall = std::get_if<SSACFG::BuiltinCall>(&op.kind);
				if (!builtinCall || builtinCall->builtin != memoryGuardHandle)
					continue;

				yulAssert(builtinCall->literalArguments.size() == 1);
				Literal const& literal = builtinCall->literalArguments.front();
				yulAssert(literal.kind == LiteralKind::Number);
				u256 const argument = literal.value.value();

				if (!offset)
					offset = argument;
				else
					yulAssert(*offset == argument, "Inconsistent memory guard args found.");
			}

	m_memoryGuardCache = offset;
	return *m_memoryGuardCache;
}
