/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <stack>
#include <map>
#include <memory>
#include <optional>
#include <functional>

#include "hrw4u/Types.h"
#include "hrw4u/Error.h"
#include "hrw4u/Tables.h"
#include "hrw4u/Visitor.h"
#include "hrw4u/HRW4UVisitor.h"
#include "hrw4uBaseVisitor.h"
#include "hrw4uParser.h"
#include "hrw4uLexer.h"

namespace hrw4u
{

class HRW4UVisitorImpl : public hrw4uBaseVisitor
{
public:
  HRW4UVisitorImpl(const FactoryCallbacks &callbacks, const ParserConfig &config);
  ~HRW4UVisitorImpl() override = default;

  HRW4UVisitorImpl(const HRW4UVisitorImpl &)            = delete;
  HRW4UVisitorImpl &operator=(const HRW4UVisitorImpl &) = delete;

  ParseResult parse(std::string_view input);

  [[nodiscard]] bool
  has_errors() const
  {
    return _errors.has_errors();
  }

  [[nodiscard]] const ErrorCollector &
  errors() const
  {
    return _errors;
  }

  std::any visitProgram(hrw4uParser::ProgramContext *ctx) override;
  std::any visitSection(hrw4uParser::SectionContext *ctx) override;
  std::any visitVarSection(hrw4uParser::VarSectionContext *ctx) override;
  std::any visitVariableDecl(hrw4uParser::VariableDeclContext *ctx) override;
  std::any visitStatement(hrw4uParser::StatementContext *ctx) override;
  std::any visitConditional(hrw4uParser::ConditionalContext *ctx) override;
  std::any visitIfStatement(hrw4uParser::IfStatementContext *ctx) override;
  std::any visitElifClause(hrw4uParser::ElifClauseContext *ctx) override;
  std::any visitElseClause(hrw4uParser::ElseClauseContext *ctx) override;
  std::any visitBlock(hrw4uParser::BlockContext *ctx) override;
  std::any visitCondition(hrw4uParser::ConditionContext *ctx) override;
  std::any visitExpression(hrw4uParser::ExpressionContext *ctx) override;
  std::any visitTerm(hrw4uParser::TermContext *ctx) override;
  std::any visitFactor(hrw4uParser::FactorContext *ctx) override;
  std::any visitComparison(hrw4uParser::ComparisonContext *ctx) override;
  std::any visitFunctionCall(hrw4uParser::FunctionCallContext *ctx) override;
  std::any visitValue(hrw4uParser::ValueContext *ctx) override;
  std::any visitModifier(hrw4uParser::ModifierContext *ctx) override;

private:
  struct IfBlockState {
    void *op_if        = nullptr;
    int   clause_index = 0;
  };

  void           add_error(antlr4::ParserRuleContext *ctx, const std::string &message);
  void           add_error(const std::string &message);
  SourceLocation get_location(antlr4::ParserRuleContext *ctx) const;

  void  start_section(SectionType type);
  void  close_section();
  void *get_or_create_ruleset();

  ParserContext build_parser_context(const std::string &op, const std::string &arg = "", const std::string &val = "");
  void         *create_condition(const ParserContext &pctx);
  void         *create_operator(const ParserContext &pctx);
  bool          add_condition_to_current(void *cond);
  bool          add_operator_to_current(void *op);

  void  process_expression(hrw4uParser::ExpressionContext *ctx, bool last, bool followed_by_or);
  void  process_term(hrw4uParser::TermContext *ctx, bool last, bool followed_by_or);
  void  process_factor(hrw4uParser::FactorContext *ctx, bool last, bool followed_by_or, bool negated = false);
  void *process_comparison(hrw4uParser::ComparisonContext *ctx, bool negated);
  void *process_function_condition(hrw4uParser::FunctionCallContext *ctx, bool negated);
  void *process_identifier_condition(const std::string &ident, bool negated);

  void process_assignment(hrw4uParser::StatementContext *ctx, const std::string &lhs, hrw4uParser::ValueContext *value_ctx,
                          bool append);
  void process_function_statement(hrw4uParser::FunctionCallContext *ctx);
  void process_break();

  std::string   extract_value_string(hrw4uParser::ValueContext *ctx);
  std::string   substitute_strings(const std::string &str, antlr4::ParserRuleContext *ctx);
  void          extract_modifiers(hrw4uParser::ModifierContext *ctx);
  std::string   get_comparison_op(hrw4uParser::ComparisonContext *ctx);
  void          cleanup_on_error();
  void          track_object(void *obj, const std::string &type);
  ResolveResult resolve_identifier(const std::string &ident);
  std::string   get_source_line(size_t line_number) const;

  const FactoryCallbacks &_callbacks;
  const ParserConfig     &_config;
  ErrorCollector          _errors;
  const SymbolResolver   &_resolver;

  std::vector<void *>                         _rulesets;
  std::vector<SectionType>                    _sections;
  std::vector<std::pair<void *, std::string>> _allocated_objects;
  std::stack<IfBlockState>                    _if_stack;
  std::stack<void *>                          _group_stack;
  std::map<std::string, Variable>             _variables;

  CondState     _cond_state;
  OperatorState _oper_state;
  SectionType   _current_section = SectionType::UNKNOWN;
  void         *_current_ruleset = nullptr;
  int           _next_var_slot   = 0;
  bool          _section_has_ops = false;

  std::vector<std::string> _source_lines;

  static constexpr int MAX_IF_DEPTH = 10;
};

class HRW4UErrorListener : public antlr4::BaseErrorListener
{
public:
  HRW4UErrorListener(ErrorCollector &errors, std::string_view filename) : _errors(errors), _filename(filename) {}

  void syntaxError(antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line, size_t charPositionInLine,
                   const std::string &msg, std::exception_ptr e) override;

private:
  ErrorCollector  &_errors;
  std::string_view _filename;
};

} // namespace hrw4u
