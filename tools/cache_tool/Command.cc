/** @file

    Nest commands (for command line processing).

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

#include "Command.h"
#include <new>
#include <algorithm>
#include <string.h>
#include <sstream>

namespace ApacheTrafficServer
{

  int CommandTable::_opt_idx = 0;

  // Error message functions.
  ts::Errata ERR_COMMAND_TAG_NOT_FOUND(char const* tag) { std::ostringstream s;
    s << "Command tag " << tag << " not found";
    return ts::Errata(s.str());}

  ts::Errata ERR_SUBCOMMAND_REQUIRED() { return ts::Errata(std::string("Incomplete command, additional keyword required")); }


  CommandTable::Command::Command()
  {
  }

  CommandTable::Command::Command(std::string const& name, std::string const& help) : _name(name), _help(help)
  {
  }

  CommandTable::Command::Command(std::string const& name, std::string const& help, CommandFunction const& f) : _name(name), _help(help), _func(f)
  {
  }

  auto CommandTable::Command::set(CommandFunction const& f) -> self&
  {
    _func = f;
    return *this;
  }

  CommandTable::Command& CommandTable::Command::subCommand(std::string const& name, std::string const& help, CommandFunction const & f)
  {
    _group.emplace_back(Command(name, help, f));
    return _group.back();
  }

  auto CommandTable::Command::subCommand(std::string const& name, std::string const& help) -> self&
  {
    _group.emplace_back(Command(name,help));
    return _group.back();
  }

  ts::Rv<bool> CommandTable::Command::invoke(int argc, char* argv[])
  {
    ts::Rv<bool> zret = true;

    if (CommandTable::_opt_idx >= argc || argv[CommandTable::_opt_idx][0] == '-') {
      // Tail of command keywords, try to invoke.
      if (_func) zret = _func(argc - CommandTable::_opt_idx, argv + CommandTable::_opt_idx);
      else zret = false, zret = ERR_SUBCOMMAND_REQUIRED();
    } else {
      char const* tag = argv[CommandTable::_opt_idx];
      auto spot = std::find_if(_group.begin(), _group.end(),
                               [tag](CommandGroup::value_type const& elt) {
                                 return 0 == strcasecmp(tag, elt._name.c_str()); } );
      if (spot != _group.end()) {
        ++CommandTable::_opt_idx;
        zret = spot->invoke(argc, argv);
      }
      else {
        zret = false;
        zret = ERR_COMMAND_TAG_NOT_FOUND(tag);
      }
    }
    return zret;
  }

  void CommandTable::Command::helpMessage(int argc, char* argv[], std::ostream& out, std::string const& prefix) const
  {

    if (CommandTable::_opt_idx >= argc || argv[CommandTable::_opt_idx][0] == '-') {
      // Tail of command keywords, start listing
      if (_name.empty()) { // root command group, don't print for that.
        for ( Command const& c : _group ) c.helpMessage(argc, argv, out, prefix);
      } else {
        out << prefix << _name << ": " << _help << std::endl;
        for ( Command const& c : _group ) c.helpMessage(argc, argv, out, "  " + prefix);
      }
    } else {
      char const* tag = argv[CommandTable::_opt_idx];
      auto spot = std::find_if(_group.begin(), _group.end(),
                               [tag](CommandGroup::value_type const& elt) {
                                 return 0 == strcasecmp(tag, elt._name.c_str()); } );
      if (spot != _group.end()) {
        ++CommandTable::_opt_idx;
        spot->helpMessage(argc, argv, out, prefix);
      } else {
        out <<  ERR_COMMAND_TAG_NOT_FOUND(tag) << std::endl;
      }
    }
  }

  CommandTable::Command::~Command() { }

  CommandTable::CommandTable()
  {
  }

  auto CommandTable::add(std::string const& name, std::string const& help) -> Command&
  {
    return _top.subCommand(name, help);
  }

  auto CommandTable::add(std::string const& name, std::string const& help, CommandFunction const& f) -> Command&
  {
    return _top.subCommand(name, help, f);
  }

  ts::Rv<bool> CommandTable::invoke(int argc, char* argv[])
  {
    _opt_idx = 0;
    return _top.invoke(argc, argv);
  }

  // This is basically cloned from invoke(), need to find how to do some unification.
  void CommandTable::helpMessage(int argc, char* argv[]) const
  {
    _opt_idx = 0;
    std::cerr << "Command tree" << std::endl;
    _top.helpMessage(argc, argv, std::cerr, std::string("* "));
  }
}
