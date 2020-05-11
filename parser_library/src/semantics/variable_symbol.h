/*
 * Copyright (c) 2019 Broadcom.
 * The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Contributors:
 *   Broadcom, Inc. - initial API and implementation
 */

#ifndef SEMANTICS_VARIABLE_SYMBOL_H
#define SEMANTICS_VARIABLE_SYMBOL_H

#include <memory>
#include <vector>

#include "antlr4-runtime.h"

#include "concatenation.h"
#include "context/id_storage.h"
#include "expressions/conditional_assembly/ca_expresssion.h"
#include "range.h"

// this file is a composition of structures that create concat_chain
// concat_chain is used to represent model statement fields

namespace hlasm_plugin {
namespace parser_library {
namespace semantics {

struct variable_symbol;
struct basic_variable_symbol;
struct created_variable_symbol;

using vs_ptr = std::unique_ptr<variable_symbol>;

struct variable_symbol
{
    const bool created;

    std::vector<antlr4::ParserRuleContext*> subscript;

    const range symbol_range;

    basic_variable_symbol* access_basic();
    const basic_variable_symbol* access_basic() const;
    created_variable_symbol* access_created();
    const created_variable_symbol* access_created() const;

protected:
    variable_symbol(const bool created, std::vector<antlr4::ParserRuleContext*> subscript, const range symbol_range);
};

struct basic_variable_symbol : public variable_symbol
{
    basic_variable_symbol(
        context::id_index name, std::vector<antlr4::ParserRuleContext*> subscript, range symbol_range);

    const context::id_index name;
};

struct created_variable_symbol : public variable_symbol
{
    created_variable_symbol(
        concat_chain created_name, std::vector<antlr4::ParserRuleContext*> subscript, range symbol_range);

    const concat_chain created_name;
};


} // namespace semantics
} // namespace parser_library
} // namespace hlasm_plugin
#endif
