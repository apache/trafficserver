/** @file

  Powerful and easy-to-use command line parsing for ATS

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

#include "tscore/ArgParser.h"
#include "tscore/ink_file.h"
#include "tscore/I_Version.h"

#include <iostream>
#include <set>
#include <sstream>
#include <sysexits.h>

std::string global_usage;
std::string parser_program_name;
std::string default_command;

// by default return EX_USAGE(64) when usage is called.
// if -h or --help is called specifically, return 0
int usage_return_code = EX_USAGE;

namespace ts
{
ArgParser::ArgParser() {}

ArgParser::ArgParser(std::string const &name, std::string const &description, std::string const &envvar, unsigned arg_num,
                     Function const &f)
{
  // initialize _top_level_command according to the provided message
  _top_level_command = ArgParser::Command(name, description, envvar, arg_num, f);
}

ArgParser::~ArgParser() {}

// add new options with args
ArgParser::Command &
ArgParser::add_option(std::string const &long_option, std::string const &short_option, std::string const &description,
                      std::string const &envvar, unsigned arg_num, std::string const &default_value, std::string const &key)
{
  return _top_level_command.add_option(long_option, short_option, description, envvar, arg_num, default_value, key);
}

// add sub-command with only function
ArgParser::Command &
ArgParser::add_command(std::string const &cmd_name, std::string const &cmd_description, Function const &f, std::string const &key)
{
  return _top_level_command.add_command(cmd_name, cmd_description, f, key);
}

// add sub-command without args and function
ArgParser::Command &
ArgParser::add_command(std::string const &cmd_name, std::string const &cmd_description, std::string const &cmd_envvar,
                       unsigned cmd_arg_num, Function const &f, std::string const &key)
{
  return _top_level_command.add_command(cmd_name, cmd_description, cmd_envvar, cmd_arg_num, f, key);
}

void
ArgParser::add_global_usage(std::string const &usage)
{
  global_usage = usage;
}

void
ArgParser::help_message(std::string_view err) const
{
  _top_level_command.help_message(err);
}

// a graceful way to output help message
void
ArgParser::Command::help_message(std::string_view err) const
{
  if (!err.empty()) {
    std::cout << "Error: " << err << std::endl;
  }
  // output global usage
  if (global_usage.size() > 0) {
    std::cout << "\nUsage: " + global_usage << std::endl;
  }
  // output subcommands
  std::cout << "\nCommands ---------------------- Description -----------------------" << std::endl;
  std::string prefix = "";
  output_command(std::cout, prefix);
  // output options
  if (_option_list.size() > 0) {
    std::cout << "\nOptions ======================= Default ===== Description =============" << std::endl;
    output_option();
  }
  // output example usage
  if (!_example_usage.empty()) {
    std::cout << "\nExample Usage: " << _example_usage << std::endl;
  }
  // standard return code
  exit(usage_return_code);
}

void
ArgParser::Command::version_message() const
{
  // unified version message of ATS
  AppVersionInfo appVersionInfo;
  appVersionInfo.setup(PACKAGE_NAME, _name.c_str(), PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  ink_fputln(stdout, appVersionInfo.FullVersionInfoStr);
  exit(0);
}

void
ArgParser::set_default_command(std::string const &cmd)
{
  if (default_command.empty()) {
    if (_top_level_command._subcommand_list.find(cmd) == _top_level_command._subcommand_list.end()) {
      std::cerr << "Error: Default command " << cmd << "not found" << std::endl;
      exit(1);
    }
    default_command = cmd;
  } else if (cmd != default_command) {
    std::cerr << "Error: Default command " << default_command << "already existed" << std::endl;
    exit(1);
  }
}

// Top level call of parsing
Arguments
ArgParser::parse(const char **argv)
{
  // deal with argv first
  int size = 0;
  _argv.clear();
  while (argv[size]) {
    _argv.push_back(argv[size]);
    size++;
  }
  if (size == 0) {
    std::cout << "Error: invalid argv provided" << std::endl;
    exit(1);
  }
  // the name of the program only
  _argv[0]                 = _argv[0].substr(_argv[0].find_last_of('/') + 1);
  _top_level_command._name = _argv[0];
  _top_level_command._key  = _argv[0];
  parser_program_name      = _argv[0];
  Arguments ret; // the parsed arg object to return
  AP_StrVec args = _argv;
  // call the recrusive parse method in Command
  if (!_top_level_command.parse(ret, args)) {
    // deal with default command
    if (!default_command.empty()) {
      args = _argv;
      args.insert(args.begin() + 1, default_command);
      _top_level_command.parse(ret, args);
    }
  };
  // if there is anything left, then output usage
  if (!args.empty()) {
    std::string msg = "Unknown command, option or args:";
    for (const auto &it : args) {
      msg = msg + " '" + it + "'";
    }
    // find the correct level to output help message
    ArgParser::Command *command = &_top_level_command;
    for (unsigned i = 1; i < _argv.size(); i++) {
      auto it = command->_subcommand_list.find(_argv[i]);
      if (it == command->_subcommand_list.end()) {
        break;
      }
      command = &it->second;
    }
    command->help_message(msg);
  }
  return ret;
}

ArgParser::Command &
ArgParser::require_commands()
{
  return _top_level_command.require_commands();
}

void
ArgParser::set_error(std::string e)
{
  _error_msg = e;
}

std::string
ArgParser::get_error() const
{
  return _error_msg;
}

//=========================== Command class ================================
ArgParser::Command::Command() {}

ArgParser::Command::~Command() {}

ArgParser::Command::Command(std::string const &name, std::string const &description, std::string const &envvar, unsigned arg_num,
                            Function const &f, std::string const &key)
  : _name(name), _description(description), _arg_num(arg_num), _envvar(envvar), _f(f), _key(key)
{
}

// check if this is a valid option before adding
void
ArgParser::Command::check_option(std::string const &long_option, std::string const &short_option, std::string const &key) const
{
  if (long_option.size() < 3 || long_option[0] != '-' || long_option[1] != '-') {
    // invalid name
    std::cerr << "Error: invalid long option added: '" + long_option + "'" << std::endl;
    exit(1);
  }
  if (short_option.size() > 2 || (short_option.size() > 0 && short_option[0] != '-')) {
    // invalid short option
    std::cerr << "Error: invalid short option added: '" + short_option + "'" << std::endl;
    exit(1);
  }
  // find if existing in option list
  if (_option_list.find(long_option) != _option_list.end()) {
    std::cerr << "Error: long option '" + long_option + "' already existed" << std::endl;
    exit(1);
  } else if (_option_map.find(short_option) != _option_map.end()) {
    std::cerr << "Error: short option '" + short_option + "' already existed" << std::endl;
    exit(1);
  }
}

// check if this is a valid command before adding
void
ArgParser::Command::check_command(std::string const &name, std::string const &key) const
{
  if (name.empty()) {
    // invalid name
    std::cerr << "Error: empty command cannot be added" << std::endl;
    exit(1);
  }
  // find if existing in subcommand list
  if (_subcommand_list.find(name) != _subcommand_list.end()) {
    std::cerr << "Error: command already exists: '" + name + "'" << std::endl;
    exit(1);
  }
}

// add new options with args
ArgParser::Command &
ArgParser::Command::add_option(std::string const &long_option, std::string const &short_option, std::string const &description,
                               std::string const &envvar, unsigned arg_num, std::string const &default_value,
                               std::string const &key)
{
  std::string lookup_key = key.empty() ? long_option.substr(2) : key;
  check_option(long_option, short_option, lookup_key);
  _option_list[long_option] = {long_option, short_option == "-" ? "" : short_option, description, envvar, arg_num, default_value,
                               lookup_key};
  if (short_option != "-" && !short_option.empty()) {
    _option_map[short_option] = long_option;
  }
  return *this;
}

// add sub-command with only function
ArgParser::Command &
ArgParser::Command::add_command(std::string const &cmd_name, std::string const &cmd_description, Function const &f,
                                std::string const &key)
{
  std::string lookup_key = key.empty() ? cmd_name : key;
  check_command(cmd_name, lookup_key);
  _subcommand_list[cmd_name] = ArgParser::Command(cmd_name, cmd_description, "", 0, f, lookup_key);
  return _subcommand_list[cmd_name];
}

// add sub-command without args and function
ArgParser::Command &
ArgParser::Command::add_command(std::string const &cmd_name, std::string const &cmd_description, std::string const &cmd_envvar,
                                unsigned cmd_arg_num, Function const &f, std::string const &key)
{
  std::string lookup_key = key.empty() ? cmd_name : key;
  check_command(cmd_name, lookup_key);
  _subcommand_list[cmd_name] = ArgParser::Command(cmd_name, cmd_description, cmd_envvar, cmd_arg_num, f, lookup_key);
  return _subcommand_list[cmd_name];
}

ArgParser::Command &
ArgParser::Command::add_example_usage(std::string const &usage)
{
  _example_usage = usage;
  return *this;
}

// method used by help_message()
void
ArgParser::Command::output_command(std::ostream &out, std::string const &prefix) const
{
  if (_name != parser_program_name) {
    // a nicely formated way to output command usage
    std::string msg = prefix + _name;
    // nicely formated output
    if (!_description.empty()) {
      if (INDENT_ONE - static_cast<int>(msg.size()) < 0) {
        // if the command msg is too long
        std::cout << msg << "\n" << std::string(INDENT_ONE, ' ') << _description << std::endl;
      } else {
        std::cout << msg << std::string(INDENT_ONE - msg.size(), ' ') << _description << std::endl;
      }
    }
  }
  // recursive call
  for (const auto &it : _subcommand_list) {
    it.second.output_command(out, "  " + prefix);
  }
}

// a nicely formatted way to output option message for help.
void
ArgParser::Command::output_option() const
{
  for (const auto &it : _option_list) {
    std::string msg;
    if (!it.second.short_option.empty()) {
      msg = it.second.short_option + ", ";
    }
    msg += it.first;
    unsigned num = it.second.arg_num;
    if (num != 0) {
      if (num == 1) {
        msg = msg + " <arg>";
      } else if (num == MORE_THAN_ZERO_ARG_N) {
        msg = msg + " [<arg> ...]";
      } else if (num == MORE_THAN_ONE_ARG_N) {
        msg = msg + " <arg> ...";
      } else {
        msg = msg + " <arg1> ... <arg" + std::to_string(num) + ">";
      }
    }
    if (!it.second.default_value.empty()) {
      if (INDENT_ONE - static_cast<int>(msg.size()) < 0) {
        msg = msg + "\n" + std::string(INDENT_ONE, ' ') + it.second.default_value;
      } else {
        msg = msg + std::string(INDENT_ONE - msg.size(), ' ') + it.second.default_value;
      }
    }
    if (!it.second.description.empty()) {
      if (INDENT_TWO - static_cast<int>(msg.size()) < 0) {
        std::cout << msg << "\n" << std::string(INDENT_TWO, ' ') << it.second.description << std::endl;
      } else {
        std::cout << msg << std::string(INDENT_TWO - msg.size(), ' ') << it.second.description << std::endl;
      }
    }
  }
}

// helper method to handle the arguments and put them nicely in arguments
// can be switched to ts::errata
static std::string
handle_args(Arguments &ret, AP_StrVec &args, std::string const &name, unsigned arg_num, unsigned &index)
{
  ArgumentData data;
  ret.append(name, data);
  // handle the args
  if (arg_num == MORE_THAN_ZERO_ARG_N || arg_num == MORE_THAN_ONE_ARG_N) {
    // infinite arguments
    if (arg_num == MORE_THAN_ONE_ARG_N && args.size() <= index + 1) {
      return "at least one argument expected by " + name;
    }
    for (unsigned j = index + 1; j < args.size(); j++) {
      ret.append_arg(name, args[j]);
    }
    args.erase(args.begin() + index, args.end());
    return "";
  }
  // finite number of argument handling
  for (unsigned j = 0; j < arg_num; j++) {
    if (args.size() < index + j + 2 || args[index + j + 1].empty()) {
      return std::to_string(arg_num) + " argument(s) expected by " + name;
    }
    ret.append_arg(name, args[index + j + 1]);
  }
  // erase the used arguments and append the data to the return structure
  args.erase(args.begin() + index, args.begin() + index + arg_num + 1);
  index -= 1;
  return "";
}

// Append the args of option to parsed data. Return true if there is any option called
void
ArgParser::Command::append_option_data(Arguments &ret, AP_StrVec &args, int index)
{
  std::map<std::string, unsigned> check_map;
  for (unsigned i = index; i < args.size(); i++) {
    // find matches of the arg
    if (args[i][0] == '-' && args[i][1] == '-' && args[i].find('=') != std::string::npos) {
      // deal with --args=
      std::string option_name = args[i].substr(0, args[i].find_first_of('='));
      std::string value       = args[i].substr(args[i].find_last_of('=') + 1);
      if (value.empty()) {
        help_message("missing argument for '" + option_name + "'");
      }
      auto it = _option_list.find(option_name);
      if (it != _option_list.end()) {
        ArgParser::Option cur_option = it->second;
        // handle environment variable
        if (!cur_option.envvar.empty()) {
          ret.set_env(cur_option.key, getenv(cur_option.envvar.c_str()) ? getenv(cur_option.envvar.c_str()) : "");
        }
        ret.append_arg(cur_option.key, value);
        check_map[cur_option.long_option] += 1;
        args.erase(args.begin() + i);
        i -= 1;
      }
    } else {
      // output version message
      if ((args[i] == "--version" || args[i] == "-V") && _option_list.find("--version") != _option_list.end()) {
        version_message();
      }
      // output help message
      if ((args[i] == "--help" || args[i] == "-h") && _option_list.find("--help") != _option_list.end()) {
        ArgParser::Command *command = this;
        // find the correct level to output help messsage
        for (unsigned i = 1; i < args.size(); i++) {
          auto it = command->_subcommand_list.find(args[i]);
          if (it == command->_subcommand_list.end()) {
            break;
          }
          command = &it->second;
        }
        usage_return_code = 0;
        command->help_message();
      }
      // deal with normal --arg val1 val2 ...
      auto long_it  = _option_list.find(args[i]);
      auto short_it = _option_map.find(args[i]);
      // long option match or short option match
      if (long_it != _option_list.end() || short_it != _option_map.end()) {
        ArgParser::Option cur_option;
        if (long_it != _option_list.end()) {
          cur_option = long_it->second;
        } else {
          cur_option = _option_list.at(short_it->second);
        }
        // handle the arguments
        std::string err = handle_args(ret, args, cur_option.key, cur_option.arg_num, i);
        if (!err.empty()) {
          help_message(err);
        }
        // handle environment variable
        if (!cur_option.envvar.empty()) {
          ret.set_env(cur_option.key, getenv(cur_option.envvar.c_str()) ? getenv(cur_option.envvar.c_str()) : "");
        }
      }
    }
  }
  // check for wrong number of arguments for --arg=...
  for (const auto &it : check_map) {
    unsigned num = _option_list.at(it.first).arg_num;
    if (num != it.second && num < MORE_THAN_ONE_ARG_N) {
      help_message(std::to_string(_option_list.at(it.first).arg_num) + " arguments expected by " + it.first);
    }
  }
  // put in the default value of options
  for (const auto &it : _option_list) {
    if (!it.second.default_value.empty() && ret.get(it.second.key).empty()) {
      std::istringstream ss(it.second.default_value);
      std::string token;
      while (std::getline(ss, token, ' ')) {
        ret.append_arg(it.second.key, token);
      }
    }
  }
}

// Main recursive logic of Parsing
bool
ArgParser::Command::parse(Arguments &ret, AP_StrVec &args)
{
  bool command_called = false;
  // iterate through all arguments
  for (unsigned i = 0; i < args.size(); i++) {
    if (_name == args[i]) {
      command_called = true;
      // handle the option
      append_option_data(ret, args, i);
      // handle the action
      if (_f) {
        ret._action = _f;
      }
      std::string err = handle_args(ret, args, _key, _arg_num, i);
      if (!err.empty()) {
        help_message(err);
      }
      // set ENV var
      if (!_envvar.empty()) {
        ret.set_env(_key, getenv(_envvar.c_str()) ? getenv(_envvar.c_str()) : "");
      }
      break;
    }
  }
  if (command_called) {
    bool flag = false;
    // recursively call subcommand
    for (auto &it : _subcommand_list) {
      if (it.second.parse(ret, args)) {
        flag = true;
        break;
      }
    }
    // check for command required
    if (!flag && _command_required) {
      help_message("No subcommand found for " + _name);
    }
    if (_name == parser_program_name) {
      // if we are at the top level
      return flag;
    }
  }
  return command_called;
}

ArgParser::Command &
ArgParser::Command::require_commands()
{
  _command_required = true;
  return *this;
}

ArgParser::Command &
ArgParser::Command::set_default()
{
  default_command = _name;
  return *this;
}

//=========================== Arguments class ================================

Arguments::Arguments() {}
Arguments::~Arguments() {}

ArgumentData
Arguments::get(std::string const &name)
{
  if (_data_map.find(name) != _data_map.end()) {
    _data_map[name]._is_called = true;
    return _data_map[name];
  }
  return ArgumentData();
}

void
Arguments::append(std::string const &key, ArgumentData const &value)
{
  // perform overwrite for now
  _data_map[key] = value;
}

void
Arguments::append_arg(std::string const &key, std::string const &value)
{
  _data_map[key]._values.push_back(value);
}

void
Arguments::set_env(std::string const &key, std::string const &value)
{
  // perform overwrite for now
  _data_map[key]._env_value = value;
}

void
Arguments::show_all_configuration() const
{
  for (const auto &it : _data_map) {
    std::cout << "name: " + it.first << std::endl;
    std::string msg;
    msg = "args value:";
    for (const auto &it_data : it.second._values) {
      msg += " " + it_data;
    }
    std::cout << msg << std::endl;
    std::cout << "env value: " + it.second._env_value << std::endl << std::endl;
  }
}

// invoke the function with the args
void
Arguments::invoke()
{
  if (_action) {
    // call the std::function
    _action();
  } else {
    throw std::runtime_error("no function to invoke");
  }
}

bool
Arguments::has_action() const
{
  return _action != nullptr;
}

//=========================== ArgumentData class ================================

std::string const &
ArgumentData::env() const noexcept
{
  return _env_value;
}

std::string const &
ArgumentData::at(unsigned index) const
{
  if (index >= _values.size()) {
    throw std::out_of_range("argument not fonud at index: " + std::to_string(index));
  }
  return _values.at(index);
}

std::string const &
ArgumentData::value() const noexcept
{
  if (_values.empty()) {
    // To prevent compiler warning
    static const std::string empty = "";
    return empty;
  }
  return _values.at(0);
}

size_t
ArgumentData::size() const noexcept
{
  return _values.size();
}

bool
ArgumentData::empty() const noexcept
{
  return _values.empty() && _env_value.empty();
}

AP_StrVec::const_iterator
ArgumentData::begin() const noexcept
{
  return _values.begin();
}

AP_StrVec::const_iterator
ArgumentData::end() const noexcept
{
  return _values.end();
}

} // namespace ts
