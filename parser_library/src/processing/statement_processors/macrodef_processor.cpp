#include "macrodef_processor.h"
#include "../../semantics/concatenation.h"
#include "../statement.h"
#include "../context_manager.h"

using namespace hlasm_plugin::parser_library;
using namespace hlasm_plugin::parser_library::processing;

macrodef_processor::macrodef_processor(context::hlasm_context& hlasm_ctx, processing_state_listener& listener, const macrodef_start_data start)
	: statement_processor(processing_kind::MACRO, hlasm_ctx),
	listener_(listener), start_(start), macro_nest_(1),curr_line_(0),expecting_prototype_(true), finished_flag_(false)
{
	result_.definition_location = hlasm_ctx.processing_stack().back();
}

processing_status macrodef_processor::get_processing_status(const semantics::instruction_si& instruction) const
{
	if (expecting_prototype_)
	{
		processing_format format(processing_kind::MACRO, processing_form::MAC);
		context::id_index id;
		if (instruction.type == semantics::instruction_si_type::EMPTY)
		{
			add_diagnostic(diagnostic_s::error_E042("", "", instruction.field_range));

			id = hlasm_ctx.ids().add("ASPACE");
		}
		else
		{
			id = instruction.type == semantics::instruction_si_type::ORD ?
				std::get<context::id_index>(instruction.value) :
				hlasm_ctx.ids().add(semantics::concatenation_point::to_string(std::get<semantics::concat_chain>(instruction.value)));
		}
		return std::make_pair(format, op_code(id,context::instruction_type::MAC));
	}
	else
	{
		if (instruction.type == semantics::instruction_si_type::ORD)
		{
			auto it = hlasm_ctx.instruction_map().find(std::get<context::id_index>(instruction.value));
			if (it != hlasm_ctx.instruction_map().end() && it->second == context::instruction::instruction_array::CA)
			{
				auto operandless = std::find_if(context::instruction::ca_instructions.begin(), context::instruction::ca_instructions.end(), 
					[&](auto& instr) {return instr.name == *it->first; })->operandless;

				processing_format format(processing_kind::MACRO, processing_form::CA,
					operandless ? operand_occurence::ABSENT : operand_occurence::PRESENT);

				return std::make_pair(format, op_code(it->first, context::instruction_type::CA));
			}
		}

		processing_format format(processing_kind::MACRO, processing_form::DEFERRED);
		return std::make_pair(format, op_code());
	}
}

void macrodef_processor::process_statement(context::shared_stmt_ptr statement)
{
	bool expecting_tmp = expecting_prototype_;

	process_statement(*statement);

	if(!expecting_tmp)
		result_.definition.push_back(statement);
}

void macrodef_processor::process_statement(context::unique_stmt_ptr statement)
{
	bool expecting_tmp = expecting_prototype_;

	process_statement(*statement);

	if (!expecting_tmp)
		result_.definition.push_back(std::move(statement));
}

void macrodef_processor::end_processing()
{
	if (!finished_flag_)
		add_diagnostic(diagnostic_s::error_E046("", *result_.prototype.macro_name, range(hlasm_ctx.processing_stack().back().pos, hlasm_ctx.processing_stack().back().pos)));

	listener_.finish_macro_definition(std::move(result_));
	finished_flag_ = true;
}

bool macrodef_processor::terminal_condition(const statement_provider_kind prov_kind) const
{
	return prov_kind == statement_provider_kind::MACRO || prov_kind == statement_provider_kind::OPEN;
}

bool macrodef_processor::finished()
{
	return finished_flag_;
}

void macrodef_processor::collect_diags() const {}

void macrodef_processor::process_statement(const context::hlasm_statement& statement)
{
	if (finished_flag_)
		throw std::runtime_error("bad operation");

	if (expecting_prototype_)
	{
		assert(statement.access_resolved());
		process_prototype(*statement.access_resolved());
		expecting_prototype_ = false;
	}
	else
	{
		if (auto res_stmt = statement.access_resolved())
		{
			process_sequence_symbol(res_stmt->label_ref());

			if (res_stmt->opcode_ref().value == macro_id)
				process_MACRO();
			else if (res_stmt->opcode_ref().value == mend_id)
				process_MEND();
			else if (res_stmt->opcode_ref().value == copy_id)
				process_COPY();
		}
		else if (auto def_stmt = statement.access_deferred())
		{
			process_sequence_symbol(def_stmt->label_ref());
		}
		else assert(false);

		++curr_line_;
	}
}

context::macro_data_ptr macrodef_processor::create_macro_data(const semantics::concat_chain& chain, context::hlasm_context& hlasm_ctx)
{
	context_manager mngr(hlasm_ctx);

	if (chain.size() == 0 || chain.size() > 1 || (chain.size() == 1 && chain[0]->type != semantics::concat_type::SUB))
		return std::make_unique<context::macro_param_data_single>(mngr.concatenate_str(chain));

	const auto& inner_chain = chain[0]->access_sub()->list;

	std::vector<context::macro_data_ptr> sublist;

	for (auto& point : inner_chain)
	{
		semantics::concat_chain tmp_chain;
		tmp_chain.push_back(semantics::concat_point_ptr(point.get()));
		sublist.push_back(create_macro_data(tmp_chain,hlasm_ctx));
		tmp_chain.back().release();
	}
	return std::make_unique<context::macro_param_data_composite>(std::move(sublist));
}

void macrodef_processor::process_prototype(const resolved_statement& statement)
{
	std::vector<context::id_index> param_names;

	//label processing
	if (statement.label_ref().type == semantics::label_si_type::VAR)
	{
		auto var = std::get<semantics::vs_ptr>(statement.label_ref().value).get();
		if (var->created || var->subscript.size() != 0)
			add_diagnostic(diagnostic_s::error_E043("", "", var->symbol_range));
		else
		{
			result_.prototype.name_param = var->access_basic()->name;
			param_names.push_back(result_.prototype.name_param);
		}
	}
	else if (statement.label_ref().type != semantics::label_si_type::EMPTY)
		add_diagnostic(diagnostic_s::error_E044("", "", statement.label_ref().field_range));

	//instr
	result_.prototype.macro_name = statement.opcode_ref().value;

	//ops
	for (auto& op : statement.operands_ref().value)
	{
		if (op->type == semantics::operand_type::EMPTY)
		{
			result_.prototype.symbolic_params.emplace_back(nullptr, nullptr);
			continue;
		}

		auto tmp = op->access_mac();
		assert(tmp);

		semantics::concatenation_point::clear_concat_chain(tmp->chain);

		if (tmp->chain.size() == 1 && tmp->chain[0]->type == semantics::concat_type::VAR) //if operand is varsym
		{
			auto var = tmp->chain[0]->access_var();

			if (var->created || !var->subscript.empty())
			{
				add_diagnostic(diagnostic_s::error_E043("", "", var->symbol_range));
				result_.prototype.symbolic_params.emplace_back(nullptr, nullptr);
				continue;
			}

			auto var_id = var->access_basic()->name;

			if (std::find(param_names.begin(), param_names.end(), var_id) != param_names.end())
			{
				add_diagnostic(diagnostic_s::error_E011("", "Symbolic parameter", tmp->operand_range));
				result_.prototype.symbolic_params.emplace_back(nullptr, nullptr);
			}
			else
			{
				param_names.push_back(var_id);
				result_.prototype.symbolic_params.emplace_back(nullptr, var_id);
			}
		}
		else if (tmp->chain.size() == 0)											//if operand is empty
		{
			result_.prototype.symbolic_params.emplace_back(nullptr, nullptr);
		}
		else if (tmp->chain.size() > 1)
		{
			if (tmp->chain[0]->type == semantics::concat_type::VAR && tmp->chain[1]->type == semantics::concat_type::EQU)	//if operand is in form of key param
			{
				auto var = tmp->chain[0]->access_var();

				if (var->created || !var->subscript.empty())
				{
					add_diagnostic(diagnostic_s::error_E043("", "", var->symbol_range));
					result_.prototype.symbolic_params.emplace_back(nullptr, nullptr);
					continue;
				}

				auto var_id = var->access_basic()->name;

				if (std::find(param_names.begin(), param_names.end(), var_id) != param_names.end())
				{
					add_diagnostic(diagnostic_s::error_E011("", "Symbolic parameter", tmp->operand_range));
				}
				else
				{
					param_names.push_back(var_id);

					tmp->chain.erase(tmp->chain.begin());
					tmp->chain.erase(tmp->chain.begin());

					result_.prototype.symbolic_params.emplace_back(create_macro_data(std::move(tmp->chain),hlasm_ctx), var_id);
				}
			}
			else
				add_diagnostic(diagnostic_s::error_E043("", "", op->operand_range));
		}
	}

}

void macrodef_processor::process_MACRO() { ++macro_nest_; }

void macrodef_processor::process_MEND() 
{
	assert(macro_nest_ != 0);
	--macro_nest_; 

	if (macro_nest_ == 0)
		finished_flag_ = true;
}

void macrodef_processor::process_COPY()
{
}

void macrodef_processor::process_sequence_symbol(const semantics::label_si& label)
{
	if (label.type == semantics::label_si_type::SEQ)
	{
		auto& seq = std::get<semantics::seq_sym>(label.value);

		if (result_.sequence_symbols.find(seq.name) != result_.sequence_symbols.end())
		{
			add_diagnostic(diagnostic_s::error_E044("", *seq.name, seq.symbol_range));
		}
		else
		{
			result_.sequence_symbols.emplace(seq.name, std::make_unique<context::macro_sequence_symbol>(seq.name, hlasm_ctx.processing_stack().back(), curr_line_));
		}
	}

}