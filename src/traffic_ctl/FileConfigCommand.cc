/** @file

  @section license License

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
#include <fstream>
#include <unordered_map>

#include "FileConfigCommand.h"
#include "tscpp/util/YamlCfg.h"
#include "swoc/TextView.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_base.h"

namespace
{
constexpr std::string_view PREFIX{"proxy.config."};
constexpr std::string_view TS_PREFIX{"ts."};

constexpr bool CREATE_IF_NOT_EXIST{true};
constexpr bool DO_NOT_CREATE_IF_NOT_EXIST{false};
const std::pair<bool, YAML::Node> NOT_FOUND{false, {}};

/// We support either passing variables with the prefix 'proxy.config.' or 'ts.'
/// Internally we need to use 'ts.variable' as the root node starts with 'ts' for records
/// configs.
std::string
amend_variable_name(swoc::TextView variable)
{
  std::string var{TS_PREFIX};
  // If the variable is prefixed with "proxy.config" we will remove it and replace it
  // with the records "ts." root name.
  if (swoc::TextView{variable}.starts_with(PREFIX)) {
    var += variable.substr(PREFIX.size());
    return var;
  }

  // you may be using "ts." already or some other name maybe for a different file.
  // we expect either `ts` or `proxy.config`
  return {variable.data(), variable.size()};
}

/// traffic_ctl should work without the need to pass the filename, so use the data
/// we have to figure out the file path. If the filename is specified in the traffic_ctl
/// arguments, then we use that.
void
fix_filename(std::string &filename)
{
  if (filename.empty()) {
    std::string sysconfdir;
    if (const char *env = getenv("PROXY_CONFIG_CONFIG_DIR")) {
      sysconfdir = Layout::get()->relative(env);
    } else {
      sysconfdir = Layout::get()->sysconfdir;
    }
    filename = Layout::get()->relative_to(sysconfdir, "records.yaml");
  }
}

/// Function to open a file if it exists, if not and is requested we will create the file.
std::string
open_file(std::string const &filename, std::fstream &fs, std::ios_base::openmode mode = std::ios::in | std::ios::out,
          bool create = false)
{
  fs.open(filename, mode);

  if (!fs.is_open()) {
    if (create) {
      fs.clear();
      if (fs.open(filename, std::ios::out); fs.is_open()) {
        return {};
      }
    }
    std::string text;
    return swoc::bwprint(text, "We couldn't open '{}': {}", filename, strerror(errno));
  }
  return {};
}

/// Bunch of mapping flags for the TAGS.
std::string
get_tag(swoc::TextView tag)
{
  static std::vector<std::pair<std::string_view, std::vector<std::string_view>>> const Str_to_Tag{
    {ts::Yaml::YAML_INT_TAG_URI,   {"int", "i", "I", "INT", "integer"}         },
    {ts::Yaml::YAML_FLOAT_TAG_URI, {"float", "f", "F", "FLOAT"}                },
    {ts::Yaml::YAML_STR_TAG_URI,   {"str", "s", "S", "STR", "string", "STRING"}}
  };

  for (auto const &[yaml_tag, strs] : Str_to_Tag) {
    if (auto found = std::find_if(std::begin(strs), std::end(strs), [&tag](auto t) { return tag == t; }); found != std::end(strs)) {
      return std::string{yaml_tag.data(), yaml_tag.size()};
    }
  }

  return std::string{tag.data(), tag.size()};
}

std::string
get_leading_comment()
{
  std::string text;
  std::time_t result = std::time(nullptr);
  return swoc::bwprint(text, "Document modified by traffic_ctl {}", std::asctime(std::localtime(&result)));
}

std::pair<bool, YAML::Node>
search_node(swoc::TextView variable, YAML::Node root, bool create)
{
  auto const key{variable.take_prefix_at('.')};
  auto const key_str = std::string{key.data(), key.size()};
  if (variable.empty()) {
    if (root.IsMap() && root[key_str]) {
      return {true, root[key_str]};
    } else {
      if (create) {
        YAML::Node n;
        root[key_str] = n;
        return {true, n}; // new one created;
      } else {
        return NOT_FOUND;
      }
    }
  }
  if (!root[key_str]) {
    if (create) {
      YAML::Node n;
      root[key_str] = n;
      return search_node(variable, n, create);
    }
  } else {
    return search_node(variable, root[key_str], create);
  }
  return NOT_FOUND;
}
} // namespace

YAML::Node
FlatYAMLAccessor::find_or_create_node(swoc::TextView variable, bool search_all)
{
  if (!search_all) {
    if (_docs.size() == 0) {
      // If nothing in it. Add one, it'll be the new node.
      _docs.emplace_back(YAML::NodeType::Map);
    }
  } else {
    for (auto iter = _docs.rbegin(); iter != _docs.rend(); ++iter) {
      if (auto [found, node] = search_node(variable, *iter, DO_NOT_CREATE_IF_NOT_EXIST); found) {
        return node;
      }
    }
    // We haven't found the node, so we will create a new field in the latest doc.
    if (_docs.size() == 0) {
      // if nothing in it. Add one, it'll be the new node.
      _docs.emplace_back(YAML::NodeType::Map);
    }
    // Use the last doc.
  }

  return search_node(variable, _docs.back(), CREATE_IF_NOT_EXIST).second;
}

std::pair<bool, YAML::Node>
FlatYAMLAccessor::find_node(swoc::TextView variable)
{
  if (_docs.size() > 0) { // make sure there is something
    // We start from the bottom.
    for (auto iter = std::rbegin(_docs); iter != std::rend(_docs); ++iter) {
      if (auto [found, node] = search_node(variable, *iter, DO_NOT_CREATE_IF_NOT_EXIST); found) {
        // found it.
        return {true, node};
      }
    }
  }

  return NOT_FOUND; // couldn't find the node;
}

void
FlatYAMLAccessor::make_tree_node(swoc::TextView variable, swoc::TextView value, swoc::TextView tag, YAML::Emitter &out)
{
  auto const key{variable.take_prefix_at('.')};
  auto const key_str = std::string{key.data(), key.size()};
  if (variable.empty()) {
    out << YAML::BeginMap << YAML::Key << key_str;
    if (!tag.empty()) {
      out << YAML::VerbatimTag(get_tag(tag));
    }
    out << YAML::Value << std::string{value.data(), value.size()} << YAML::EndMap;
  } else {
    out << YAML::BeginMap << YAML::Key << key_str;
    make_tree_node(variable, value, tag, out);
    out << YAML::EndMap;
  }
}

FileConfigCommand::FileConfigCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};
  _printer = std::make_unique<GenericPrinter>(printOpts);
  if (args->get(SET_STR)) {
    _invoked_func = [&]() { config_set(); };
  } else if (args->get(GET_STR)) {
    _invoked_func = [&]() { config_get(); };
  } else {
    throw std::invalid_argument("Can't deal with the provided arguments");
  }
}

void
FileConfigCommand::config_get()
{
  auto filename = get_parsed_arguments()->get(COLD_STR).value(); // could be empty which means we should use the default file name
  auto const &data = get_parsed_arguments()->get(GET_STR);
  std::string text;

  fix_filename(filename);
  try {
    FlatYAMLAccessor::load(YAML::LoadAllFromFile(filename));

    for (auto const &var : data) { // we support multiple get's
      std::string variable = amend_variable_name(var);
      auto [found, search] = find_node(variable);

      if (found) {
        _printer->write_output(swoc::bwprint(text, "{}: {}", var, search.as<std::string>()));
      } else {
        _printer->write_output(swoc::bwprint(text, "{} not found", var));
      }
    }
  } catch (YAML::Exception const &ex) {
    throw std::logic_error(swoc::bwprint(text, "config get error: {}", ex.what()));
  }
}

void
FileConfigCommand::config_set()
{
  static const std::string CREATE_STR{"create"};
  static const std::string TYPE_STR{"type"};

  auto filename = get_parsed_arguments()->get(COLD_STR).value(); // could be empty which means we should use the default file name
  bool append   = !get_parsed_arguments()->get(UPDATE_STR);
  auto const &data = get_parsed_arguments()->get(SET_STR);

  std::string passed_tag;
  if (auto p = get_parsed_arguments()->get(TYPE_STR); p) {
    passed_tag = p.value();
  }
  // Get the default records.yaml if nothing is passed.
  fix_filename(filename);
  if (filename.empty()) {
    throw std::logic_error("Can't deduce the file path.");
  }

  std::ios_base::openmode mode = std::ios::in | std::ios::out;
  if (append) {
    mode = std::ios::out | std::ios::app;
  }

  std::fstream fs;
  if (auto err = open_file(filename, fs, mode, true); !err.empty()) {
    throw std::logic_error(err);
  }

  std::string const &variable = amend_variable_name(data[0]);
  try {
    if (append) {
      YAML::Emitter doc; // we will build the document again, either to append the
                         // new node or to modify the existing one.
      doc << YAML::Comment(get_leading_comment()) << YAML::BeginDoc;
      make_tree_node(variable, data[1], passed_tag, doc);
      doc << YAML::Newline;

      fs.write(doc.c_str(), doc.size());
      fs.close();
    } else {
      FlatYAMLAccessor::load(YAML::LoadAll(fs));
      fs.close();

      auto new_node = find_or_create_node(variable);

      new_node = data[1];

      if (!passed_tag.empty()) {
        new_node.SetTag(get_tag(passed_tag));
      }
      // try gain
      if (auto err = open_file(filename, fs, std::ios::out | std::ios::trunc); !err.empty()) {
        throw std::logic_error(err);
      }

      YAML::Emitter doc;
      YAML::Node last_node;
      if (_docs.size() > 0) {
        last_node = _docs.back();
        _docs.pop_back();
      }

      for (auto const &n : _docs) {
        if (!n.IsNull()) {
          doc << n;
        }
      }

      if (doc.size() > 0) {
        // There is something already, so add a new line.
        doc << YAML::Newline;
      }
      doc << YAML::Comment(get_leading_comment()) << YAML::BeginDoc << last_node << YAML::Newline;

      fs.write(doc.c_str(), doc.size());
      fs.close();
    }
    std::string text;
    _printer->write_output(swoc::bwprint(text, "Set {}", variable));
  } catch (std::exception const &ex) {
    if (fs.is_open()) {
      fs.close();
    }
    throw ex;
  }
}
