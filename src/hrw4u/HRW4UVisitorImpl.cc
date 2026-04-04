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

#include "HRW4UVisitorImpl.h"

#include <regex>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "swoc/TextView.h"

#include "hrw4uLexer.h"
#include "hrw4uParser.h"

#include "atn/ParserATNSimulator.h"
#include "atn/PredictionMode.h"

namespace hrw4u
{

namespace
{
  inline std::string
  strip_quotes(const std::string &s)
  {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
      return s.substr(1, s.size() - 2);
    }

    return s;
  }
} // namespace

HRW4UVisitorImpl::HRW4UVisitorImpl(const FactoryCallbacks &callbacks, const ParserConfig &config)
  : _callbacks(callbacks), _config(config), _resolver(symbol_resolver())
{
}

ParseResult
HRW4UVisitorImpl::parse(std::string_view input)
{
  ParseResult result;

  _source_lines.clear();
  swoc::TextView remaining(input);

  while (remaining) {
    _source_lines.emplace_back(remaining.take_prefix_at('\n'));
  }

  try {
    std::string              input_str(input);
    antlr4::ANTLRInputStream input_stream(input_str);
    hrw4uLexer               lexer(&input_stream);
    HRW4UErrorListener       lexer_error_listener(_errors, _config.filename);

    lexer.removeErrorListeners();
    lexer.addErrorListener(&lexer_error_listener);

    antlr4::CommonTokenStream tokens(&lexer);
    hrw4uParser               parser(&tokens);
    HRW4UErrorListener        parser_error_listener(_errors, _config.filename);

    parser.removeErrorListeners();
    parser.addErrorListener(&parser_error_listener);

    parser.getInterpreter<antlr4::atn::ParserATNSimulator>()->setPredictionMode(antlr4::atn::PredictionMode::SLL);

    hrw4uParser::ProgramContext *tree = parser.program();

    if (!_errors.has_errors()) {
      visit(tree);
      close_section();
    }

    if (_errors.has_errors()) {
      cleanup_on_error();
      result.success = false;
    } else {
      result.success  = true;
      result.rulesets = std::move(_rulesets);
      result.sections = std::move(_sections);
    }
    result.errors = std::move(_errors);

  } catch (const std::exception &e) {
    add_error(std::string("Parse error: ") + e.what());
    cleanup_on_error();
    result.success = false;
    result.errors  = std::move(_errors);
  }

  return result;
}

void
HRW4UVisitorImpl::add_error(antlr4::ParserRuleContext *ctx, const std::string &message)
{
  _errors.add_error(ParseError{.message = message, .location = get_location(ctx)});
}

void
HRW4UVisitorImpl::add_error(const std::string &message)
{
  _errors.add_error(ParseError{.message = message, .location = {.filename = _config.filename}});
}

SourceLocation
HRW4UVisitorImpl::get_location(antlr4::ParserRuleContext *ctx) const
{
  SourceLocation loc;

  loc.filename = _config.filename;
  if (ctx && ctx->start) {
    loc.line    = ctx->start->getLine();
    loc.column  = ctx->start->getCharPositionInLine();
    loc.context = get_source_line(loc.line);
    if (ctx->stop) {
      loc.length = ctx->stop->getStopIndex() - ctx->start->getStartIndex() + 1;
    } else {
      loc.length = ctx->start->getText().size();
    }
  }
  return loc;
}

std::string
HRW4UVisitorImpl::get_source_line(size_t line_number) const
{
  if (line_number > 0 && line_number <= _source_lines.size()) {
    return _source_lines[line_number - 1];
  }
  return "";
}

void
HRW4UVisitorImpl::start_section(SectionType type)
{
  close_section();
  _current_section = type;
  _current_ruleset = nullptr;
  _section_has_ops = false;
}

void
HRW4UVisitorImpl::close_section()
{
  if (_current_ruleset != nullptr) {
    _rulesets.push_back(_current_ruleset);
    _sections.push_back(_current_section);
    _current_ruleset = nullptr;
  }
  _current_section = SectionType::UNKNOWN;
}

void *
HRW4UVisitorImpl::get_or_create_ruleset()
{
  if (_current_ruleset == nullptr && _callbacks.create_ruleset) {
    _current_ruleset = _callbacks.create_ruleset();
    track_object(_current_ruleset, "ruleset");

    if (_current_ruleset && _callbacks.set_ruleset_hook) {
      SectionType effective_section = (_current_section != SectionType::UNKNOWN) ? _current_section : _config.default_hook;
      _callbacks.set_ruleset_hook(_current_ruleset, static_cast<int>(effective_section));
    }
  }
  return _current_ruleset;
}

ParserContext
HRW4UVisitorImpl::build_parser_context(const std::string &op, const std::string &arg, const std::string &val)
{
  ParserContext ctx;

  ctx.op       = op;
  ctx.arg      = arg;
  ctx.val      = val;
  ctx.from_url = _config.from_url;
  ctx.to_url   = _config.to_url;

  for (const auto &m : _cond_state.to_list()) {
    ctx.mods.push_back(m);
  }
  for (const auto &m : _oper_state.to_list()) {
    ctx.mods.push_back(m);
  }

  return ctx;
}

void *
HRW4UVisitorImpl::create_condition(const ParserContext &pctx)
{
  if (!_callbacks.create_condition) {
    add_error("No condition factory callback configured");
    return nullptr;
  }
  void *cond = _callbacks.create_condition(pctx);
  if (cond) {
    track_object(cond, "condition");
  }
  return cond;
}

void *
HRW4UVisitorImpl::create_operator(const ParserContext &pctx)
{
  if (!_callbacks.create_operator) {
    add_error("No operator factory callback configured");
    return nullptr;
  }
  void *op = _callbacks.create_operator(pctx);
  if (op) {
    track_object(op, "operator");
  }
  return op;
}

bool
HRW4UVisitorImpl::add_condition_to_current(void *cond)
{
  if (!cond) {
    return false;
  }

  // Groups are ConditionGroup* objects, not RuleSet*, so we need a separate callback
  if (!_group_stack.empty()) {
    void *group_ptr = _group_stack.top();

    if (group_ptr && _callbacks.add_condition_to_group) {
      return _callbacks.add_condition_to_group(group_ptr, cond);
    }
    return false;
  }

  if (!_if_stack.empty()) {
    IfBlockState &state = _if_stack.top();

    if (state.op_if && _callbacks.add_condition_to_if) {
      return _callbacks.add_condition_to_if(state.op_if, cond);
    }
    // If op_if is nullptr, fall through to add to RuleSet (section-level if/elif/else)
  }

  void *ruleset = get_or_create_ruleset();

  if (!ruleset || !_callbacks.add_condition) {
    return false;
  }
  return _callbacks.add_condition(ruleset, cond);
}

bool
HRW4UVisitorImpl::add_operator_to_current(void *op)
{
  if (!op) {
    return false;
  }

  if (!_if_stack.empty()) {
    IfBlockState &state = _if_stack.top();

    if (state.op_if && _callbacks.add_operator_to_if) {
      return _callbacks.add_operator_to_if(state.op_if, op);
    }
    // If op_if is nullptr, fall through to add to RuleSet (section-level if/elif/else)
  }

  void *ruleset = get_or_create_ruleset();
  if (!ruleset || !_callbacks.add_operator) {
    return false;
  }
  _section_has_ops = true;
  return _callbacks.add_operator(ruleset, op);
}

void
HRW4UVisitorImpl::cleanup_on_error()
{
  if (_callbacks.destroy) {
    for (auto &[obj, type] : _allocated_objects) {
      if (obj && type == "ruleset") {
        _callbacks.destroy(obj, type);
      }
    }
  }

  _allocated_objects.clear();
  _rulesets.clear();
  _sections.clear();
  _current_ruleset = nullptr;
}

void
HRW4UVisitorImpl::track_object(void *obj, const std::string &type)
{
  if (obj) {
    _allocated_objects.emplace_back(obj, type);
  }
}

ResolveResult
HRW4UVisitorImpl::resolve_identifier(const std::string &ident)
{
  auto it = _variables.find(ident);

  if (it != _variables.end()) {
    const VarTypeInfo &info = var_type_info(it->second.type);
    ResolveResult      result;

    result.target =
      std::string(it->second.cond_prefix()) + "-" + std::string(info.cond_tag) + ":" + std::to_string(it->second.slot);
    result.success = true;

    return result;
  }

  return _resolver.resolve_condition(ident, _current_section);
}

std::string
HRW4UVisitorImpl::extract_value_string(hrw4uParser::ValueContext *ctx)
{
  if (!ctx) {
    return "";
  }

  if (ctx->str) {
    return strip_quotes(ctx->str->getText());
  }
  if (ctx->number) {
    return ctx->number->getText();
  }
  if (ctx->ident) {
    return ctx->ident->getText();
  }
  if (ctx->TRUE()) {
    return "true";
  }
  if (ctx->FALSE()) {
    return "false";
  }
  if (ctx->ip()) {
    return ctx->ip()->getText();
  }
  if (ctx->iprange()) {
    return ctx->iprange()->getText();
  }

  if (ctx->paramRef()) {
    std::string param_name = ctx->paramRef()->IDENT()->getText();
    auto        binding    = _proc_bindings.find(param_name);

    if (binding != _proc_bindings.end()) {
      return binding->second;
    }
    add_error(ctx, "'$" + param_name + "' used outside procedure context");

    return "";
  }

  return ctx->getText();
}

std::string
HRW4UVisitorImpl::substitute_strings(const std::string &str, antlr4::ParserRuleContext * /* ctx */)
{
  if (str.size() < 2 || str.front() != '"' || str.back() != '"') {
    return str;
  }

  try {
    std::string             inner = str.substr(1, str.size() - 2);
    std::string             result;
    static const std::regex pattern(R"(\{([a-zA-Z_][a-zA-Z0-9_.-]*(?:\([^)]*\))?)\})");
    std::smatch             match;
    std::string             remaining = inner;

    while (std::regex_search(remaining, match, pattern)) {
      if (match.position() > 0 && remaining[match.position() - 1] == '%') {
        result    += remaining.substr(0, match.position() + match.length());
        remaining  = remaining.substr(match.position() + match.length());
        continue;
      }

      result += match.prefix();

      std::string content   = match[1].str();
      size_t      paren_pos = content.find('(');

      if (paren_pos != std::string::npos) {
        std::string func_name = content.substr(0, paren_pos);
        std::string args_str  = content.substr(paren_pos + 1);

        if (!args_str.empty() && args_str.back() == ')') {
          args_str.pop_back();
        }

        auto func_result = _resolver.resolve_function(func_name, _current_section);

        if (func_result.success) {
          std::string replacement = func_result.target;

          if (!args_str.empty()) {
            replacement += ":" + args_str;
          }
          result += "%{" + replacement + "}";
        } else {
          result += "{" + content + "}";
        }
      } else {
        auto var_it = _variables.find(content);

        if (var_it != _variables.end()) {
          const VarTypeInfo &info = var_type_info(var_it->second.type);

          result += "%{" + std::string(var_it->second.cond_prefix()) + "-" + std::string(info.cond_tag) + ":" +
                    std::to_string(var_it->second.slot) + "}";
        } else {
          auto cond_result = _resolver.resolve_condition(content, _current_section);

          if (cond_result.success) {
            std::string resolved = cond_result.target;

            if (!cond_result.suffix.empty()) {
              resolved += ":" + cond_result.suffix;
            }

            if (resolved.starts_with("%{") && resolved.ends_with("}")) {
              result += resolved;
            } else {
              result += "%{" + resolved + "}";
            }
          } else {
            result += "{" + content + "}";
          }
        }
      }

      remaining = match.suffix();
    }

    result += remaining;

    if (!_proc_bindings.empty()) {
      std::string    substituted;
      swoc::TextView scan(result);

      while (scan) {
        auto dollar = scan.find('$');

        if (dollar == swoc::TextView::npos) {
          substituted += scan;
          break;
        }

        substituted += scan.prefix(dollar);
        scan.remove_prefix(dollar + 1); // skip past '$'

        // Extract identifier: [a-zA-Z_][a-zA-Z0-9_-]*
        size_t len = 0;

        if (scan && (std::isalpha(scan[0]) || scan[0] == '_')) {
          ++len;
          while (len < scan.size() && (std::isalnum(scan[len]) || scan[len] == '_' || scan[len] == '-')) {
            ++len;
          }
        }

        if (len > 0) {
          std::string param_name(scan.prefix(len));
          auto        binding = _proc_bindings.find(param_name);

          if (binding != _proc_bindings.end()) {
            substituted += binding->second;
          } else {
            substituted += '$';
            substituted += param_name;
          }
          scan.remove_prefix(len);
        } else {
          substituted += '$';
        }
      }
      result = std::move(substituted);
    }

    return "\"" + result + "\"";
  } catch (const std::exception &e) {
    add_error("String substitution error: " + std::string(e.what()));
    return str;
  }
}

void
HRW4UVisitorImpl::extract_modifiers(hrw4uParser::ModifierContext *ctx)
{
  if (!ctx || !ctx->modifierList()) {
    return;
  }

  for (auto *token : ctx->modifierList()->mods) {
    std::string mod = token->getText();

    std::transform(mod.begin(), mod.end(), mod.begin(), [](unsigned char c) { return std::toupper(c); });
    if (ModifierInfo::is_condition_modifier(mod)) {
      _cond_state.add_modifier(mod);
    } else if (ModifierInfo::is_operator_modifier(mod)) {
      _oper_state.add_modifier(mod);
    } else {
      add_error(ctx, "Unknown modifier: " + mod);
    }
  }
}

std::string
HRW4UVisitorImpl::get_comparison_op(hrw4uParser::ComparisonContext *ctx)
{
  if (ctx->children.size() > 1) {
    std::string op_text = ctx->children[1]->getText();

    if (op_text == "==" || op_text == "=") {
      return "=";
    }
    if (op_text == "!=" || op_text == "!~") {
      return "=";
    }
    if (op_text == ">" || op_text == "<") {
      return op_text;
    }
  }
  return "=";
}

std::any
HRW4UVisitorImpl::visitProgram(hrw4uParser::ProgramContext *ctx)
{
  bool seen_sections = false;

  for (auto *item : ctx->programItem()) {
    if (item->commentLine()) {
      continue;
    }

    if (item->useDirective()) {
      if (seen_sections) {
        add_error(item->useDirective(), "'use' directives must appear before any section blocks");
        continue;
      }
      visitUseDirective(item->useDirective());
      continue;
    }

    if (item->procedureDecl()) {
      if (seen_sections) {
        add_error(item->procedureDecl(), "'procedure' declarations must appear before any section blocks");
        continue;
      }
      visitProcedureDecl(item->procedureDecl());
      continue;
    }

    if (item->section()) {
      seen_sections = true;
      visit(item->section());
    }
  }

  close_section();
  return {};
}

std::any
HRW4UVisitorImpl::visitSection(hrw4uParser::SectionContext *ctx)
{
  if (ctx->varSection()) {
    return visit(ctx->varSection());
  }

  if (ctx->sessionVarSection()) {
    return visitSessionVarSection(ctx->sessionVarSection());
  }

  if (!ctx->name) {
    add_error(ctx, "Missing section name");
    return {};
  }

  std::string section_name = ctx->name->getText();
  auto        section_type = _resolver.resolve_hook(section_name);

  if (!section_type) {
    add_error(ctx, "Invalid section name: " + section_name);
    return {};
  }

  start_section(*section_type);

  bool in_statement_block = false;

  for (size_t idx = 0; idx < ctx->sectionBody().size(); ++idx) {
    auto *body           = ctx->sectionBody()[idx];
    bool  is_conditional = body->conditional() != nullptr;
    bool  is_comment     = body->commentLine() != nullptr;

    if (is_comment) {
      continue;
    } else if (is_conditional) {
      if (idx > 0) {
        close_section();
        start_section(*section_type);
      }
      visit(body->conditional());
      in_statement_block = false;
    } else {
      if (!in_statement_block) {
        if (idx > 0) {
          close_section();
          start_section(*section_type);
        }
        in_statement_block = true;
      }

      if (auto *stmt = body->statement()) {
        visit(stmt);
      }
    }
  }

  return {};
}

std::any
HRW4UVisitorImpl::visitVarSection(hrw4uParser::VarSectionContext *ctx)
{
  if (_current_section != SectionType::UNKNOWN) {
    add_error(ctx, "Variable section must appear before any hook section");
    return {};
  }

  if (ctx->variables()) {
    for (auto *item : ctx->variables()->variablesItem()) {
      if (item->commentLine()) {
        continue;
      }
      if (item->variableDecl()) {
        visit(item->variableDecl());
      }
    }
  }

  return {};
}

std::any
HRW4UVisitorImpl::visitSessionVarSection(hrw4uParser::SessionVarSectionContext *ctx)
{
  if (_current_section != SectionType::UNKNOWN) {
    add_error(ctx, "Session variable section must appear before any hook section");
    return {};
  }

  _current_var_scope = VarScope::SESSION;

  if (ctx->variables()) {
    for (auto *item : ctx->variables()->variablesItem()) {
      if (item->commentLine()) {
        continue;
      }
      if (item->variableDecl()) {
        visit(item->variableDecl());
      }
    }
  }

  _current_var_scope = VarScope::TXN;

  return {};
}

std::string
HRW4UVisitorImpl::resolve_use_path(const std::string &spec)
{
  namespace fs = std::filesystem;

  // Map "foo::bar::Baz" to "foo/bar/Baz.hrw4u"
  std::string rel = spec;

  for (std::string::size_type pos = 0; (pos = rel.find("::", pos)) != std::string::npos;) {
    rel.replace(pos, 2, "/");
  }
  rel += ".hrw4u";

  for (const auto &base : _config.procedures_paths) {
    fs::path candidate = fs::path(base) / rel;
    if (fs::is_regular_file(candidate)) {
      return fs::canonical(candidate).string();
    }
  }

  return "";
}

std::vector<ProcedureParam>
HRW4UVisitorImpl::collect_proc_params(hrw4uParser::ParamListContext *ctx)
{
  std::vector<ProcedureParam> params;

  if (!ctx) {
    return params;
  }
  for (auto *p : ctx->param()) {
    ProcedureParam param;

    param.name        = p->IDENT()->getText();
    param.default_ctx = p->value();
    params.push_back(std::move(param));
  }

  return params;
}

std::any
HRW4UVisitorImpl::visitUseDirective(hrw4uParser::UseDirectiveContext *ctx)
{
  std::string spec = ctx->QUALIFIED_IDENT()->getText();

  if (_config.procedures_paths.empty()) {
    add_error(ctx, "'use' directive requires --procedures-path (no search paths configured)");
    return {};
  }

  std::string path = resolve_use_path(spec);

  if (path.empty()) {
    add_error(ctx, "Cannot find procedure file for 'use " + spec + "'");
    return {};
  }

  std::vector<std::string> load_stack;

  load_proc_file(path, load_stack, spec);
  return {};
}

void
HRW4UVisitorImpl::load_proc_file(const std::string &path, std::vector<std::string> &load_stack, const std::string &use_spec)
{
  namespace fs = std::filesystem;

  std::string abs_path = fs::canonical(path).string();

  if (_proc_loaded.contains(abs_path)) {
    return;
  }

  for (const auto &entry : load_stack) {
    if (entry == abs_path) {
      std::string cycle = use_spec.empty() ? abs_path : use_spec;

      for (const auto &s : load_stack) {
        cycle += " -> " + s;
      }
      cycle += " -> " + (use_spec.empty() ? abs_path : use_spec);
      add_error("Circular 'use' dependency: " + cycle);
      return;
    }
  }

  _proc_loaded.insert(abs_path);

  std::ifstream infile(abs_path);

  if (!infile.is_open()) {
    add_error("Cannot open procedure file: " + abs_path);
    return;
  }

  std::stringstream buffer;

  buffer << infile.rdbuf();
  std::string content = buffer.str();

  auto input_stream = std::make_shared<antlr4::ANTLRInputStream>(content);
  auto lexer        = std::make_shared<hrw4uLexer>(input_stream.get());
  auto stream       = std::make_shared<antlr4::CommonTokenStream>(lexer.get());

  stream->fill();
  auto parser = std::make_shared<hrw4uParser>(stream.get());

  parser->removeErrorListeners();

  auto *tree = parser->program();

  std::string expected_prefix;

  if (!use_spec.empty()) {
    auto last_sep = use_spec.rfind("::");
    if (last_sep != std::string::npos) {
      expected_prefix = use_spec.substr(0, last_sep + 2);
    }
  }

  load_stack.push_back(abs_path);

  bool found_proc = false;
  bool found_use  = false;

  for (auto *item : tree->programItem()) {
    if (item->useDirective()) {
      std::string sub_spec = item->useDirective()->QUALIFIED_IDENT()->getText();
      std::string sub_path = resolve_use_path(sub_spec);

      if (sub_path.empty()) {
        add_error("Cannot find procedure file for 'use " + sub_spec + "' (referenced from " + abs_path + ")");
        continue;
      }
      load_proc_file(sub_path, load_stack, sub_spec);
      found_use = true;
      continue;
    }

    if (item->procedureDecl()) {
      auto       *decl = item->procedureDecl();
      std::string name = decl->QUALIFIED_IDENT()->getText();

      if (name.find("::") == std::string::npos) {
        add_error("Procedure name must be qualified (contain '::'): " + name);
        continue;
      }

      if (!expected_prefix.empty() && !name.starts_with(expected_prefix)) {
        add_error("Procedure '" + name + "' does not match expected namespace '" + expected_prefix + "' from 'use " + use_spec +
                  "'");
        continue;
      }

      if (_proc_registry.contains(name)) {
        add_error("Duplicate procedure name: " + name);
        continue;
      }

      ProcedureSignature sig;

      sig.qualified_name = name;
      sig.params         = collect_proc_params(decl->paramList());
      sig.body_ctx       = decl->block();
      sig.source_file    = abs_path;
      sig.input_stream   = input_stream;
      sig.lexer          = lexer;
      sig.token_stream   = stream;
      sig.parser         = parser;

      _proc_registry[name] = std::move(sig);
      found_proc           = true;
      continue;
    }

    if (item->section()) {
      add_error("Procedure files cannot contain section blocks (found in " + abs_path + ")");
    }
  }

  load_stack.pop_back();

  if (!found_proc && !found_use && !use_spec.empty()) {
    add_error("No procedures found in file loaded by 'use " + use_spec + "'");
  }
}

std::any
HRW4UVisitorImpl::visitProcedureDecl(hrw4uParser::ProcedureDeclContext *ctx)
{
  std::string name = ctx->QUALIFIED_IDENT()->getText();

  if (name.find("::") == std::string::npos) {
    add_error(ctx, "Procedure name must be qualified (contain '::'): " + name);
    return {};
  }

  if (_proc_registry.contains(name)) {
    add_error(ctx, "Duplicate procedure name: " + name);
    return {};
  }

  ProcedureSignature sig;

  sig.qualified_name = name;
  sig.params         = collect_proc_params(ctx->paramList());
  sig.body_ctx       = ctx->block();
  sig.source_file    = _config.filename;

  _proc_registry[name] = std::move(sig);
  return {};
}

void
HRW4UVisitorImpl::expand_procedure_call(const std::string &name, hrw4uParser::ArgumentListContext *args_ctx,
                                        antlr4::ParserRuleContext *call_ctx)
{
  auto it = _proc_registry.find(name);

  if (it == _proc_registry.end()) {
    add_error(call_ctx, "Unknown procedure '" + name + "': not loaded via 'use'");
    return;
  }

  const ProcedureSignature &sig = it->second;

  for (const auto &entry : _proc_call_stack) {
    if (entry == name) {
      std::string cycle = name;

      for (const auto &s : _proc_call_stack) {
        cycle += " -> " + s;
      }
      cycle += " -> " + name;
      add_error(call_ctx, "Circular procedure call: " + cycle);
      return;
    }
  }

  std::vector<std::string> call_args;

  if (args_ctx) {
    for (auto *val : args_ctx->value()) {
      std::string arg = extract_value_string(val);

      arg = strip_quotes(arg);
      call_args.push_back(std::move(arg));
    }
  }

  size_t required = 0;

  for (const auto &p : sig.params) {
    if (!p.default_ctx) {
      ++required;
    }
  }

  if (call_args.size() < required || call_args.size() > sig.params.size()) {
    add_error(call_ctx, "Procedure '" + name + "' expects " +
                          (required == sig.params.size() ? std::to_string(required) :
                                                           std::to_string(required) + "-" + std::to_string(sig.params.size())) +
                          " argument(s), got " + std::to_string(call_args.size()));
    return;
  }

  std::map<std::string, std::string> bindings;

  for (size_t i = 0; i < sig.params.size(); ++i) {
    if (i < call_args.size()) {
      bindings[sig.params[i].name] = call_args[i];
    } else if (sig.params[i].default_ctx) {
      std::string def = extract_value_string(sig.params[i].default_ctx);

      def                          = strip_quotes(def);
      bindings[sig.params[i].name] = std::move(def);
    }
  }

  auto saved_bindings = std::move(_proc_bindings);
  auto saved_stack    = _proc_call_stack;

  _proc_bindings = std::move(bindings);
  _proc_call_stack.push_back(name);

  if (sig.body_ctx) {
    bool at_section_level = _if_stack.empty();

    if (at_section_level) {
      bool        in_statement_block = _section_has_ops;
      SectionType proc_section       = _current_section;

      for (size_t idx = 0; idx < sig.body_ctx->blockItem().size(); ++idx) {
        auto *block_item = sig.body_ctx->blockItem()[idx];

        if (block_item->commentLine()) {
          continue;
        } else if (block_item->conditional()) {
          if (in_statement_block || idx > 0) {
            close_section();
            start_section(proc_section);
          }
          visitConditional(block_item->conditional());
          in_statement_block = false;
        } else if (block_item->statement()) {
          if (!in_statement_block && idx > 0) {
            close_section();
            start_section(proc_section);
          }
          visitStatement(block_item->statement());
          in_statement_block = true;
        }
      }
    } else {
      for (auto *block_item : sig.body_ctx->blockItem()) {
        if (block_item->statement()) {
          visitStatement(block_item->statement());
        } else if (block_item->conditional()) {
          visitConditional(block_item->conditional());
        }
      }
    }
  }

  _proc_bindings   = std::move(saved_bindings);
  _proc_call_stack = std::move(saved_stack);
}

std::any
HRW4UVisitorImpl::visitVariableDecl(hrw4uParser::VariableDeclContext *ctx)
{
  if (!ctx->name || !ctx->typeName) {
    add_error(ctx, "Variable declaration requires name and type");
    return {};
  }

  std::string name      = ctx->name->getText();
  std::string type_name = ctx->typeName->getText();

  if (name.find('.') != std::string::npos || name.find(':') != std::string::npos) {
    add_error(ctx, "Variable name cannot contain '.' or ':': " + name);
    return {};
  }

  auto var_type = _resolver.resolve_var_type(type_name);

  if (!var_type) {
    add_error(ctx, "Invalid variable type: " + type_name);
    return {};
  }

  int slot = -1;

  if (ctx->slot) {
    slot = std::stoi(ctx->slot->getText());
  } else {
    std::set<int> used_slots;

    for (const auto &[vname, var] : _variables) {
      if (var.type == *var_type && var.scope == _current_var_scope) {
        used_slots.insert(var.slot);
      }
    }
    for (int i = 0;; ++i) {
      if (!used_slots.contains(i)) {
        slot = i;
        break;
      }
    }
  }
  _variables[name] = Variable{.name = name, .type = *var_type, .slot = slot, .scope = _current_var_scope};

  return {};
}

std::any
HRW4UVisitorImpl::visitStatement(hrw4uParser::StatementContext *ctx)
{
  if (ctx->BREAK()) {
    process_break();
    return {};
  }

  if (ctx->functionCall()) {
    process_function_statement(ctx->functionCall());
    return {};
  }

  if (ctx->EQUAL() && ctx->lhs && ctx->value()) {
    process_assignment(ctx, ctx->lhs->getText(), ctx->value(), false);
    return {};
  }

  if (ctx->PLUSEQUAL() && ctx->lhs && ctx->value()) {
    process_assignment(ctx, ctx->lhs->getText(), ctx->value(), true);
    return {};
  }

  if (ctx->op) {
    auto          result = _resolver.resolve_statement_function(ctx->op->getText(), _current_section);
    ParserContext pctx   = build_parser_context(result.success ? result.target : ctx->op->getText());
    pctx.op_type         = result.op_type;
    void *op             = create_operator(pctx);

    if (op) {
      add_operator_to_current(op);
    }

    return {};
  }

  add_error(ctx, "Unrecognized statement");
  return {};
}

void
HRW4UVisitorImpl::process_break()
{
  _oper_state.last_modifier = true;
  ParserContext pctx        = build_parser_context("no-op");
  pctx.op_type              = hrw::OperatorType::NO_OP;
  void *op                  = create_operator(pctx);

  if (op) {
    add_operator_to_current(op);
  }
  _oper_state.reset();
}

void
HRW4UVisitorImpl::process_assignment(hrw4uParser::StatementContext *stmt_ctx, const std::string &lhs,
                                     hrw4uParser::ValueContext *value_ctx, bool is_append)
{
  std::string raw_rhs = value_ctx->getText();
  std::string rhs;

  if (raw_rhs.size() >= 2 && raw_rhs.front() == '"' && raw_rhs.back() == '"') {
    rhs = strip_quotes(substitute_strings(raw_rhs, value_ctx));
  } else {
    rhs = extract_value_string(value_ctx);
  }

  auto var_it = _variables.find(lhs);

  if (var_it != _variables.end()) {
    if (is_append) {
      add_error(stmt_ctx, "Cannot use += operator with variables");
      return;
    }

    const Variable    &var       = var_it->second;
    const VarTypeInfo &info      = var_type_info(var.type);
    std::string        rhs_value = rhs;

    if (value_ctx && value_ctx->ident) {
      auto rhs_var_it = _variables.find(rhs);

      if (rhs_var_it != _variables.end()) {
        auto resolve_result = resolve_identifier(rhs);

        if (resolve_result.success) {
          rhs_value = "%{" + resolve_result.target + "}";
        }
      }
    }

    std::string op_tag = std::string(var.op_prefix()) + "-" + to_lower(info.cond_tag);

    ParserContext pctx = build_parser_context(op_tag, std::to_string(var.slot), rhs_value);
    pctx.op_type       = info.op_type;
    void *oper         = create_operator(pctx);

    if (oper) {
      add_operator_to_current(oper);
    }

    return;
  }

  auto result = _resolver.resolve_operator(lhs, _current_section);

  if (!result.success) {
    add_error(stmt_ctx, "Cannot resolve operator for: " + lhs + " - " + result.error_message);
    return;
  }

  ParserContext pctx = build_parser_context("", result.suffix.empty() ? rhs : result.suffix, result.suffix.empty() ? "" : rhs);
  pctx.op_type       = result.get_operator_type(is_append, rhs.empty());
  void *op           = create_operator(pctx);

  if (op) {
    add_operator_to_current(op);
  }
}

void
HRW4UVisitorImpl::process_function_statement(hrw4uParser::FunctionCallContext *ctx)
{
  if (!ctx->funcName) {
    add_error(ctx, "Missing function name");
    return;
  }

  std::string func_name = ctx->funcName->getText();

  if (func_name.find("::") != std::string::npos) {
    if (_proc_registry.contains(func_name)) {
      expand_procedure_call(func_name, ctx->argumentList(), ctx);
      return;
    }
    add_error(ctx, "Unknown procedure '" + func_name + "': not loaded via 'use'");
    return;
  }

  std::vector<std::string> args;

  if (ctx->argumentList()) {
    for (auto *val : ctx->argumentList()->value()) {
      std::string raw_arg = val->getText();

      if (raw_arg.size() >= 2 && raw_arg.front() == '"' && raw_arg.back() == '"') {
        args.push_back(strip_quotes(substitute_strings(raw_arg, val)));
      } else {
        args.push_back(extract_value_string(val));
      }
    }
  }

  auto result = _resolver.resolve_statement_function(func_name, _current_section);

  if (!result.success) {
    add_error(ctx, "Unknown function: " + func_name);
    return;
  }

  std::string arg = args.empty() ? "" : args[0];
  std::string val;

  for (size_t i = 1; i < args.size(); ++i) {
    if (!val.empty()) {
      val += " ";
    }
    val += args[i];
  }

  std::string first_arg;

  if (!result.target.empty()) {
    first_arg = result.target;
    if (!arg.empty()) {
      first_arg += " " + arg;
    }
  } else {
    first_arg = arg;
  }

  ParserContext pctx = build_parser_context("", first_arg, val);

  pctx.op_type = result.op_type;
  if (func_name == "keep_query") {
    pctx.mods.push_back("I");
  }

  void *op = create_operator(pctx);

  if (op) {
    add_operator_to_current(op);
  }
}

std::any
HRW4UVisitorImpl::visitConditional(hrw4uParser::ConditionalContext *ctx)
{
  bool is_section_level = _if_stack.empty();
  bool has_elif_else    = !ctx->elifClause().empty() || ctx->elseClause();

  if (is_section_level && !has_elif_else) {
    if (ctx->ifStatement()) {
      if (ctx->ifStatement()->condition()) {
        visit(ctx->ifStatement()->condition());
      }

      // Push marker so nested conditionals know they're not at section level
      IfBlockState state{.op_if = nullptr, .clause_index = 0};

      _if_stack.push(state);
      if (ctx->ifStatement()->block()) {
        visit(ctx->ifStatement()->block());
      }
      _if_stack.pop();
    }
  } else if (is_section_level && has_elif_else) {
    IfBlockState state{.op_if = nullptr, .clause_index = 0};

    _if_stack.push(state);
    if (ctx->ifStatement()) {
      if (ctx->ifStatement()->condition()) {
        visit(ctx->ifStatement()->condition());
      }
      if (ctx->ifStatement()->block()) {
        visit(ctx->ifStatement()->block());
      }
    }

    for (auto *elif_ctx : ctx->elifClause()) {
      if (_callbacks.new_ruleset_section) {
        void *ruleset = get_or_create_ruleset();
        void *group   = _callbacks.new_ruleset_section(ruleset, CondClause::ELIF);

        if (group) {
          _group_stack.push(group);
          visit(elif_ctx->condition());
          visit(elif_ctx->block());
          _group_stack.pop();
        }
      }
    }

    if (ctx->elseClause()) {
      if (_callbacks.new_ruleset_section) {
        void *ruleset = get_or_create_ruleset();
        void *group   = _callbacks.new_ruleset_section(ruleset, CondClause::ELSE);

        if (group) {
          _group_stack.push(group);
          visit(ctx->elseClause()->block());
          _group_stack.pop();
        }
      }
    }

    _if_stack.pop();
  } else {
    if (!has_elif_else) {
      visit(ctx->ifStatement());
    } else {
      void *op_if = nullptr;

      if (_callbacks.create_if_operator) {
        op_if = _callbacks.create_if_operator();
        if (op_if) {
          track_object(op_if, "operator_if");
        }
      }

      IfBlockState state{.op_if = op_if, .clause_index = 0};

      _if_stack.push(state);

      if (ctx->ifStatement()) {
        if (ctx->ifStatement()->condition()) {
          visit(ctx->ifStatement()->condition());
        }
        if (ctx->ifStatement()->block()) {
          visit(ctx->ifStatement()->block());
        }
      }

      for (auto *elif_ctx : ctx->elifClause()) {
        if (_callbacks.new_section && op_if) {
          void *group = _callbacks.new_section(op_if, CondClause::ELIF);

          if (group) {
            _group_stack.push(group);
            visit(elif_ctx->condition());
            visit(elif_ctx->block());
            _group_stack.pop();
          }
        }
      }

      if (ctx->elseClause()) {
        if (_callbacks.new_section && op_if) {
          void *group = _callbacks.new_section(op_if, CondClause::ELSE);

          if (group) {
            _group_stack.push(group);
            visit(ctx->elseClause()->block());
            _group_stack.pop();
          }
        }
      }

      _if_stack.pop();

      if (op_if) {
        add_operator_to_current(op_if);
      }
    }
  }

  return {};
}

std::any
HRW4UVisitorImpl::visitIfStatement(hrw4uParser::IfStatementContext *ctx)
{
  void *op_if = nullptr;

  if (_callbacks.create_if_operator) {
    op_if = _callbacks.create_if_operator();
    if (op_if) {
      track_object(op_if, "operator_if");
    }
  }

  IfBlockState state{.op_if = op_if, .clause_index = 0};

  _if_stack.push(state);

  visit(ctx->condition());
  visit(ctx->block());

  _if_stack.pop();
  if (op_if) {
    add_operator_to_current(op_if);
  }

  return {};
}

std::any
HRW4UVisitorImpl::visitElifClause(hrw4uParser::ElifClauseContext *ctx)
{
  visit(ctx->condition());
  visit(ctx->block());
  return {};
}

std::any
HRW4UVisitorImpl::visitElseClause(hrw4uParser::ElseClauseContext *ctx)
{
  visit(ctx->block());
  return {};
}

std::any
HRW4UVisitorImpl::visitBlock(hrw4uParser::BlockContext *ctx)
{
  for (auto *item : ctx->blockItem()) {
    if (item->commentLine()) {
      continue;
    } else if (item->statement()) {
      visit(item->statement());
    } else if (item->conditional()) {
      visit(item->conditional());
    }
  }
  return {};
}

std::any
HRW4UVisitorImpl::visitCondition(hrw4uParser::ConditionContext *ctx)
{
  _cond_state.reset();
  process_expression(ctx->expression(), true, false);
  return {};
}

void
HRW4UVisitorImpl::process_expression(hrw4uParser::ExpressionContext *ctx, bool last, bool followed_by_or)
{
  if (ctx->OR()) {
    process_expression(ctx->expression(), false, true);
    process_term(ctx->term(), last, followed_by_or);
  } else {
    process_term(ctx->term(), last, followed_by_or);
  }
}

void
HRW4UVisitorImpl::process_term(hrw4uParser::TermContext *ctx, bool last, bool followed_by_or)
{
  if (ctx->AND()) {
    process_term(ctx->term(), false, false);
    process_factor(ctx->factor(), last, followed_by_or);
  } else {
    process_factor(ctx->factor(), last, followed_by_or);
  }
}

void
HRW4UVisitorImpl::process_factor(hrw4uParser::FactorContext *ctx, bool last, bool followed_by_or, bool negated)
{
  if (ctx->children.size() == 2 && ctx->children[0]->getText() == "!") {
    auto *inner_factor = dynamic_cast<hrw4uParser::FactorContext *>(ctx->children[1]);

    if (inner_factor) {
      process_factor(inner_factor, last, followed_by_or, !negated);
      return;
    }
  }

  if (ctx->LPAREN()) {
    if (!last) {
      if (followed_by_or) {
        _cond_state.or_modifier = true;
      } else {
        _cond_state.add_modifier("AND");
      }
    }
    _cond_state.not_modifier = negated;
    ParserContext pctx_group = build_parser_context("%{GROUP}");
    void         *group      = create_condition(pctx_group);

    if (!group) {
      return;
    }

    add_condition_to_current(group);
    _cond_state.reset();
    _group_stack.push(group);
    process_expression(ctx->expression(), true, false);
    _group_stack.pop();

    return;
  }

  if (ctx->comparison()) {
    if (!last) {
      if (followed_by_or) {
        _cond_state.or_modifier = true;
      } else {
        _cond_state.add_modifier("AND");
      }
    }

    void *cond = process_comparison(ctx->comparison(), negated);

    if (cond) {
      add_condition_to_current(cond);
    }
    return;
  }

  if (ctx->functionCall()) {
    if (!last) {
      if (followed_by_or) {
        _cond_state.or_modifier = true;
      } else {
        _cond_state.add_modifier("AND");
      }
    }

    void *cond = process_function_condition(ctx->functionCall(), negated);

    if (cond) {
      add_condition_to_current(cond);
    }
    return;
  }

  if (ctx->TRUE()) {
    _cond_state.not_modifier = negated;

    if (!last) {
      if (followed_by_or) {
        _cond_state.or_modifier = true;
      } else {
        _cond_state.add_modifier("AND");
      }
    }

    ParserContext pctx = build_parser_context("TRUE");
    void         *cond = create_condition(pctx);

    if (cond) {
      add_condition_to_current(cond);
    }
    _cond_state.reset();
    return;
  }

  if (ctx->FALSE()) {
    _cond_state.not_modifier = negated;

    if (!last) {
      if (followed_by_or) {
        _cond_state.or_modifier = true;
      } else {
        _cond_state.add_modifier("AND");
      }
    }

    ParserContext pctx = build_parser_context("FALSE");
    void         *cond = create_condition(pctx);

    if (cond) {
      add_condition_to_current(cond);
    }
    _cond_state.reset();
    return;
  }

  if (ctx->ident) {
    if (!last) {
      if (followed_by_or) {
        _cond_state.or_modifier = true;
      } else {
        _cond_state.add_modifier("AND");
      }
    }

    void *cond = process_identifier_condition(ctx->ident->getText(), negated);

    if (cond) {
      add_condition_to_current(cond);
    }
    return;
  }
}

void *
HRW4UVisitorImpl::process_comparison(hrw4uParser::ComparisonContext *ctx, bool negated)
{
  auto *comp = ctx->comparable();

  if (!comp) {
    add_error(ctx, "Missing comparable in comparison");
    return nullptr;
  }

  std::string        lhs_resolved;
  hrw::ConditionType cond_type = hrw::ConditionType::NONE;

  if (comp->ident) {
    auto        result = resolve_identifier(comp->ident->getText());
    std::string ident  = comp->ident->getText();

    if (!result.success) {
      add_error(ctx, "Unknown condition symbol: " + ident);
      return nullptr;
    }
    lhs_resolved = result.target;
    cond_type    = result.cond_type;
    if (!result.suffix.empty()) {
      lhs_resolved += ":" + result.suffix;
    }
  } else if (comp->functionCall()) {
    if (!comp->functionCall()->funcName) {
      add_error(ctx, "Missing function name in comparison");
      return nullptr;
    }

    std::string              func_name = comp->functionCall()->funcName->getText();
    std::vector<std::string> func_args;

    if (comp->functionCall()->argumentList()) {
      for (auto *val : comp->functionCall()->argumentList()->value()) {
        func_args.push_back(extract_value_string(val));
      }
    }

    auto result = _resolver.resolve_function(func_name, _current_section);

    if (!result.success) {
      add_error(ctx, "Unknown function in comparison: " + func_name);
      return nullptr;
    }

    lhs_resolved = result.target;
    cond_type    = result.cond_type;
    if (!func_args.empty()) {
      lhs_resolved += ":" + func_args[0];
      for (size_t i = 1; i < func_args.size(); ++i) {
        lhs_resolved += "," + func_args[i];
      }
    }
  } else {
    add_error(ctx, "Invalid comparable");
    return nullptr;
  }

  std::string op = lhs_resolved;
  std::string arg;

  if (ctx->children.size() < 2) {
    add_error(ctx, "Missing operator in comparison");
    return nullptr;
  }

  std::string op_text    = ctx->children[1]->getText();
  bool        is_negated = negated;

  if (op_text == "!=" || op_text == "!~" || op_text == "!") {
    is_negated = !is_negated;
  }

  if (ctx->value()) {
    std::string rhs = extract_value_string(ctx->value());

    if (op_text == "==" || op_text == "=" || op_text == "!=") {
      arg = "=" + rhs;
    } else if (op_text == ">" || op_text == "<") {
      arg = op_text + rhs;
    }
  } else if (ctx->regex()) {
    arg = ctx->regex()->getText();
  } else if (ctx->set()) {
    std::string set_text = ctx->set()->getText();

    if (!set_text.empty() && set_text[0] == '[') {
      set_text[0]                     = '(';
      set_text[set_text.length() - 1] = ')';
    }
    arg = set_text;
  } else if (ctx->iprange()) {
    arg = ctx->iprange()->getText();
  }

  if (ctx->modifier()) {
    extract_modifiers(ctx->modifier());
  }

  _cond_state.not_modifier = is_negated;

  ParserContext pctx = build_parser_context(op, arg);
  pctx.cond_type     = cond_type;
  void *cond         = create_condition(pctx);

  _cond_state.reset();
  return cond;
}

void *
HRW4UVisitorImpl::process_function_condition(hrw4uParser::FunctionCallContext *ctx, bool negated)
{
  if (!ctx->funcName) {
    add_error(ctx, "Missing function name");
    return nullptr;
  }

  std::string              func_name = ctx->funcName->getText();
  std::vector<std::string> args;

  if (ctx->argumentList()) {
    for (auto *val : ctx->argumentList()->value()) {
      args.push_back(extract_value_string(val));
    }
  }

  auto result = _resolver.resolve_function(func_name, _current_section);

  if (!result.success) {
    add_error(ctx, "Unknown function: " + func_name);
    return nullptr;
  }

  std::string op = result.target;

  if (!args.empty()) {
    op += ":" + args[0];
    for (size_t i = 1; i < args.size(); ++i) {
      op += "," + args[i];
    }
  }

  _cond_state.not_modifier = negated;

  ParserContext pctx = build_parser_context(op);
  pctx.cond_type     = result.cond_type;
  void *cond         = create_condition(pctx);

  _cond_state.reset();

  return cond;
}

void *
HRW4UVisitorImpl::process_identifier_condition(const std::string &ident, bool negated)
{
  auto result = resolve_identifier(ident);

  if (!result.success) {
    add_error("Cannot resolve identifier: " + ident + " - " + result.error_message);
    return nullptr;
  }

  std::string op = result.target;
  std::string arg;

  if (!result.suffix.empty()) {
    op += ":" + result.suffix;
  }

  bool actual_negation = negated;

  if (result.prefix) {
    arg             = "=";
    actual_negation = !negated;
  }

  _cond_state.not_modifier = actual_negation;

  ParserContext pctx = build_parser_context(op, arg);
  pctx.cond_type     = result.cond_type;
  void *cond         = create_condition(pctx);

  _cond_state.reset();

  return cond;
}

std::any
HRW4UVisitorImpl::visitExpression(hrw4uParser::ExpressionContext *)
{
  return {};
}

std::any
HRW4UVisitorImpl::visitTerm(hrw4uParser::TermContext *)
{
  return {};
}

std::any
HRW4UVisitorImpl::visitFactor(hrw4uParser::FactorContext *)
{
  return {};
}

std::any
HRW4UVisitorImpl::visitComparison(hrw4uParser::ComparisonContext *)
{
  return {};
}

std::any
HRW4UVisitorImpl::visitFunctionCall(hrw4uParser::FunctionCallContext *)
{
  return {};
}

std::any
HRW4UVisitorImpl::visitValue(hrw4uParser::ValueContext *ctx)
{
  return extract_value_string(ctx);
}

std::any
HRW4UVisitorImpl::visitModifier(hrw4uParser::ModifierContext *ctx)
{
  extract_modifiers(ctx);
  return {};
}

void
HRW4UErrorListener::syntaxError(antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
                                size_t charPositionInLine, const std::string &msg, std::exception_ptr)
{
  ParseError error;

  error.message           = msg;
  error.location.filename = std::string(_filename);
  error.location.line     = line;
  error.location.column   = charPositionInLine;

  try {
    antlr4::CharStream *input = nullptr;

    if (auto *lexer = dynamic_cast<antlr4::Lexer *>(recognizer)) {
      input = lexer->getInputStream();
    } else if (auto *parser = dynamic_cast<antlr4::Parser *>(recognizer)) {
      auto *tokenStream = parser->getTokenStream();

      if (tokenStream && tokenStream->getTokenSource()) {
        input = tokenStream->getTokenSource()->getInputStream();
      }
    }

    if (input) {
      std::string text         = input->toString();
      size_t      pos          = 0;
      size_t      current_line = 1;

      // Find the start of the requested line
      while (current_line < line && pos < text.size()) {
        if (text[pos] == '\n') {
          ++current_line;
        }
        ++pos;
      }

      size_t line_start = pos;

      while (pos < text.size() && text[pos] != '\n') {
        ++pos;
      }
      error.location.context = text.substr(line_start, pos - line_start);
    }
  } catch (...) {
    if (offendingSymbol) {
      error.location.context = offendingSymbol->getText();
    }
  }

  _errors.add_error(error);
}

HRW4UVisitor::HRW4UVisitor(const FactoryCallbacks &callbacks, const ParserConfig &config)
  : _impl(std::make_unique<HRW4UVisitorImpl>(callbacks, config))
{
}

HRW4UVisitor::~HRW4UVisitor() = default;

HRW4UVisitor::HRW4UVisitor(HRW4UVisitor &&) noexcept = default;

HRW4UVisitor &HRW4UVisitor::operator=(HRW4UVisitor &&) noexcept = default;

ParseResult
HRW4UVisitor::parse(std::string_view input)
{
  return _impl->parse(input);
}

ParseResult
HRW4UVisitor::parse_file(std::string_view filename)
{
  std::string   fname{filename};
  std::ifstream infile{fname};

  if (!infile.is_open()) {
    ParseResult result;

    result.success = false;
    result.errors.add_error(ParseError{.message = "Cannot open file: " + fname});
    return result;
  }

  std::stringstream buffer;

  buffer << infile.rdbuf();
  return parse(buffer.str());
}

bool
HRW4UVisitor::has_errors() const
{
  return _impl->has_errors();
}

const ErrorCollector &
HRW4UVisitor::errors() const
{
  return _impl->errors();
}

void
CondState::reset()
{
  not_modifier    = false;
  or_modifier     = false;
  and_modifier    = false;
  last_modifier   = false;
  nocase_modifier = false;
  ext_modifier    = false;
  pre_modifier    = false;
  mid_modifier    = false;
  suf_modifier    = false;
}

void
CondState::add_modifier(std::string_view mod)
{
  if (mod == "NOT" || mod == "N") {
    not_modifier = true;
  } else if (mod == "OR" || mod == "O") {
    or_modifier = true;
  } else if (mod == "AND") {
    and_modifier = true;
  } else if (mod == "L" || mod == "LAST") {
    last_modifier = true;
  } else if (mod == "NC" || mod == "NOCASE" || mod == "I") {
    nocase_modifier = true;
  } else if (mod == "EXT") {
    ext_modifier = true;
  } else if (mod == "PRE") {
    pre_modifier = true;
  } else if (mod == "MID") {
    mid_modifier = true;
  } else if (mod == "SUF") {
    suf_modifier = true;
  }
}

std::vector<std::string>
CondState::to_list() const
{
  std::vector<std::string> result;

  if (not_modifier) {
    result.push_back("NOT");
  }
  if (or_modifier) {
    result.push_back("OR");
  }
  if (and_modifier) {
    result.push_back("AND");
  }
  if (last_modifier) {
    result.push_back("L");
  }
  if (nocase_modifier) {
    result.push_back("NOCASE");
  }
  if (ext_modifier) {
    result.push_back("EXT");
  }
  if (pre_modifier) {
    result.push_back("PRE");
  }
  if (mid_modifier) {
    result.push_back("MID");
  }
  if (suf_modifier) {
    result.push_back("SUF");
  }
  return result;
}

void
OperatorState::reset()
{
  last_modifier = false;
  qsa_modifier  = false;
  inv_modifier  = false;
}

void
OperatorState::add_modifier(std::string_view mod)
{
  if (mod == "L" || mod == "LAST") {
    last_modifier = true;
  } else if (mod == "QSA") {
    qsa_modifier = true;
  } else if (mod == "INV") {
    inv_modifier = true;
  }
}

std::vector<std::string>
OperatorState::to_list() const
{
  std::vector<std::string> result;

  if (last_modifier) {
    result.push_back("L");
  }
  if (qsa_modifier) {
    result.push_back("QSA");
  }
  if (inv_modifier) {
    result.push_back("INV");
  }

  return result;
}

bool
ModifierInfo::is_condition_modifier(std::string_view mod)
{
  return mod == "NOT" || mod == "N" || mod == "OR" || mod == "O" || mod == "AND" || mod == "NC" || mod == "NOCASE" || mod == "I" ||
         mod == "EXT" || mod == "PRE" || mod == "MID" || mod == "SUF";
}

bool
ModifierInfo::is_operator_modifier(std::string_view mod)
{
  return mod == "L" || mod == "LAST" || mod == "QSA" || mod == "INV";
}

} // namespace hrw4u
