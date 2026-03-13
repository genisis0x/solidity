#include <test/libsolidity/util/StandardJSONCompiler.h>

#include <vector>

#include <fmt/base.h>

#include <range/v3/view/transform.hpp>
#include <range/v3/view/enumerate.hpp>

#include <libevmasm/Assembly.h>
#include <libevmasm/AssemblyItem.h>

#include <liblangutil/Exceptions.h>
#include <liblangutil/SourceLocation.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <libsolidity/interface/DebugSettings.h>
#include <libsolidity/interface/StandardCompiler.h>
#include <libsolidity/util/Common.h>
#include <libsolutil/JSON.h>

#include <test/libsolidity/util/SoltestErrors.h>

using namespace solidity;
using namespace solidity::evmasm;
using namespace solidity::frontend::json;
using namespace solidity::frontend::test;
using namespace solidity::langutil;
using namespace solidity::util;

std::vector<output::Contract const*> const StandardJSONOutputExt::contracts() const
{
	return m_base.contracts() | ranges::views::values | ranges::views::join | ranges::views::transform([](auto const& contract) {
		return &contract;
	}) | ranges::to<std::vector>();
}

output::Contract const* StandardJSONOutputExt::contract(ContractName const& _name) const
{
	auto const& sourceUnits = m_base.contracts();
	auto const [sourceName, contractName] = std::pair{std::string{_name.source()}, _name.contract()};

	auto source = sourceUnits.find(sourceName);
	if (source == sourceUnits.end())
	    return nullptr;

	auto const& contracts = source->second;
	if (contractName.empty() && contracts.empty())
    	return nullptr;

	auto order = m_compilationOrder.find(sourceName);
	if (order == m_compilationOrder.end() || order->second.empty())
		return nullptr;

	auto lookupName = contractName.empty() ? order->second.back() : _name;
	auto contract = ranges::find_if(contracts, [&](auto const& contract) { return contract.name() == lookupName; });

	return (contract != contracts.end()) ? &*contract : nullptr;
}

