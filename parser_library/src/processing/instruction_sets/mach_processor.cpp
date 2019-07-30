#include "mach_processor.h"
#include "postponed_statement_impl.h"
#include "../context_manager.h"
#include "../../context/instruction.h"
#include "../../parser_impl.h"

#include <algorithm>

using namespace hlasm_plugin::parser_library;
using namespace hlasm_plugin::parser_library::processing;

mach_processor::mach_processor(context::hlasm_context& hlasm_ctx, branching_provider& provider, statement_field_reparser& parser)
	:low_language_processor(hlasm_ctx, provider,parser) {}

void mach_processor::process(context::shared_stmt_ptr stmt)
{
	process(preprocess(stmt), stmt->access_resolved()->opcode_ref());
}

void mach_processor::process(context::unique_stmt_ptr stmt)
{
	auto opcode = stmt->access_resolved()->opcode_ref();
	process(preprocess(std::move(stmt)), opcode);
}

void mach_processor::process(rebuilt_statement stmt, const op_code& opcode)
{
	auto mnem_tmp = context::instruction::mnemonic_codes.find(*opcode.value);

	auto tmp = mnem_tmp != context::instruction::mnemonic_codes.end() ?
		context::instruction::machine_instructions.find(mnem_tmp->second.instruction) :
		context::instruction::machine_instructions.find(*opcode.value);

	assert(tmp != context::instruction::machine_instructions.end());

	auto& [name, instr] = *tmp;

	if (stmt.label_ref().type == semantics::label_si_type::ORD)
	{
		auto label_name = context_manager(hlasm_ctx).get_symbol_name(std::get<std::string>(stmt.label_ref().value));
		if (hlasm_ctx.ord_ctx.symbol_defined(label_name))
		{
			add_diagnostic(diagnostic_s::error_E031("", "symbol", stmt.label_ref().field_range));
		}
		else
		{
			auto addr = hlasm_ctx.ord_ctx.align(context::no_align);

			hlasm_ctx.ord_ctx.create_symbol(label_name, addr, context::symbol_attributes::make_machine_attrs(instr->size_for_alloc / 8));

			if (!addr.spaces.empty())
				add_dependency(stmt.stmt_range_ref(), label_name, std::move(addr), nullptr);
		}
	}

	std::vector<const context::resolvable*> dependencies;
	for (auto& op : stmt.operands_ref().value)
	{
		auto expr_op = &*dynamic_cast<semantics::expr_machine_operand*>(op.get());
		if (expr_op)
		{
			auto evaluable = &*expr_op->expression;
			if (evaluable && evaluable->get_dependencies(hlasm_ctx.ord_ctx).contains_dependencies())
				dependencies.push_back(evaluable);
		}
	}

	if (!dependencies.empty())
		add_dependency(stmt.stmt_range_ref(), context::id_storage::empty_id, dependencies, std::make_unique<postponed_statement_impl>(std::move(stmt), hlasm_ctx.processing_stack()));
	else
		check(stmt, hlasm_ctx, checker, *this);

	hlasm_ctx.ord_ctx.reserve_storage_area(instr->size_for_alloc / 8, context::no_align);
}