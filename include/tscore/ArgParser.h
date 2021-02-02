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

#pragma once

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <string_view>

// more than zero arguments
constexpr unsigned MORE_THAN_ZERO_ARG_N = ~0;
// more than one arguments
constexpr unsigned MORE_THAN_ONE_ARG_N = ~0 - 1;
// customizable indent for help message
constexpr int INDENT_ONE = 32;
constexpr int INDENT_TWO = 46;

namespace ts
{
using AP_StrVec = std::vector<std::string>;
// The class holding both the ENV and String arguments
class ArgumentData
{
public:
  // bool to check if certain command/option is called
  operator bool() const noexcept { return _is_called; }
  // index accessing []
  std::string const &
  operator[](int x) const
  {
    return _values.at(x);
  }
  // return the Environment variable
  std::string const &env() const noexcept;
  // iterator for arguments
  AP_StrVec::const_iterator begin() const noexcept;
  AP_StrVec::const_iterator end() const noexcept;
  // index accessing
  std::string const &at(unsigned index) const;
  // access the first index, equivalent to at(0)
  std::string const &value() const noexcept;
  // size of _values
  size_t size() const noexcept;
  // return true if _values and _env_value are both empty
  bool empty() const noexcept;

private:
  bool _is_called = false;
  // the environment variable
  std::string _env_value;
  // the arguments stored
  AP_StrVec _values;

  friend class Arguments;
};

// The class holding all the parsed data after ArgParser::parse()
class Arguments
{
public:
  Arguments();
  ~Arguments();

  ArgumentData get(std::string const &name);

  void append(std::string const &key, ArgumentData const &value);
  // Append value to the arg to the map of key
  void append_arg(std::string const &key, std::string const &value);
  // append env value to the map with key
  void set_env(std::string const &key, std::string const &value);
  // Print all we have in the parsed data to the console
  void show_all_configuration() const;
  /** Invoke the function associated with the parsed command.
      @return The return value of the executed command (int).
  */
  void invoke();
  // return true if there is any function to invoke
  bool has_action() const;

private:
  // A map of all the called parsed args/data
  // Key: "command/option", value: ENV and args
  std::map<std::string, ArgumentData> _data_map;
  // The function associated. invoke() will call this func
  std::function<void()> _action;

  friend class ArgParser;
  friend class ArgumentData;
};

// Class of the ArgParser
class ArgParser
{
  using Function = std::function<void()>;

public:
  // Option structure: e.x. --arg -a
  // Contains all information about certain option(--switch)
  struct Option {
    std::string long_option;   // long option: --arg
    std::string short_option;  // short option: -a
    std::string description;   // help description
    std::string envvar;        // stored ENV variable
    unsigned arg_num;          // number of argument expected
    std::string default_value; // default value of option
    std::string key;           // look-up key
  };

  // Class for commands in a nested way
  class Command
  {
  public:
    // Constructor and destructor
    Command();
    ~Command();
    /** Add an option to current command
        @return The Option object.
    */
    Command &add_option(std::string const &long_option, std::string const &short_option, std::string const &description,
                        std::string const &envvar = "", unsigned arg_num = 0, std::string const &default_value = "",
                        std::string const &key = "");

    /** Two ways of adding a sub-command to current command:
        @return The new sub-command instance.
    */
    Command &add_command(std::string const &cmd_name, std::string const &cmd_description, Function const &f = nullptr,
                         std::string const &key = "");
    Command &add_command(std::string const &cmd_name, std::string const &cmd_description, std::string const &cmd_envvar,
                         unsigned cmd_arg_num, Function const &f = nullptr, std::string const &key = "");
    /** Add an example usage of current command for the help message
        @return The Command instance for chained calls.
    */
    Command &add_example_usage(std::string const &usage);
    /** Require subcommand/options for this command
        @return The Command instance for chained calls.
    */
    Command &require_commands();
    /** set the current command as default
        @return The Command instance for chained calls.
    */
    Command &set_default();

  protected:
    // Main constructor called by add_command()
    Command(std::string const &name, std::string const &description, std::string const &envvar, unsigned arg_num, Function const &f,
            std::string const &key = "");
    // Helper method for add_option to check the validity of option
    void check_option(std::string const &long_option, std::string const &short_option, std::string const &key) const;
    // Helper method for add_command to check the validity of command
    void check_command(std::string const &name, std::string const &key) const;
    // Helper method for ArgParser::help_message
    void output_command(std::ostream &out, std::string const &prefix) const;
    // Helper method for ArgParser::help_message
    void output_option() const;
    // Helper method for ArgParser::parse
    bool parse(Arguments &ret, AP_StrVec &args);
    // The help & version messages
    void help_message(std::string_view err = "") const;
    void version_message() const;
    // Helper method for parse()
    void append_option_data(Arguments &ret, AP_StrVec &args, int index);
    // The command name and help message
    std::string _name;
    std::string _description;

    // Expected argument number
    unsigned _arg_num = 0;
    // Stored Env variable
    std::string _envvar;
    // An example usage can be added for the help message
    std::string _example_usage;
    // Function associated with this command
    Function _f;
    // look up key
    std::string _key;

    // list of all subcommands of current command
    // Key: command name. Value: Command object
    std::map<std::string, Command> _subcommand_list;
    // list of all options of current command
    // Key: option name. Value: Option object
    std::map<std::string, Option> _option_list;
    // Map for fast searching: <short option: long option>
    std::map<std::string, std::string> _option_map;

    // require command / option for this parser
    bool _command_required = false;

    friend class ArgParser;
  };
  // Base class constructors and destructor
  ArgParser();
  ArgParser(std::string const &name, std::string const &description, std::string const &envvar, unsigned arg_num,
            Function const &f);
  ~ArgParser();

  /** Add an option to current command with arguments
      @return The Option object.
  */
  Command &add_option(std::string const &long_option, std::string const &short_option, std::string const &description,
                      std::string const &envvar = "", unsigned arg_num = 0, std::string const &default_value = "",
                      std::string const &key = "");

  /** Two ways of adding command to the parser:
      @return The new command instance.
  */
  Command &add_command(std::string const &cmd_name, std::string const &cmd_description, Function const &f = nullptr,
                       std::string const &key = "");
  Command &add_command(std::string const &cmd_name, std::string const &cmd_description, std::string const &cmd_envvar,
                       unsigned cmd_arg_num, Function const &f = nullptr, std::string const &key = "");
  // give a default command to this parser
  void set_default_command(std::string const &cmd);
  /** Main parsing function
      @return The Arguments object available for program using
  */
  Arguments parse(const char **argv);
  // Add the usage to global_usage for help_message(). Something like: traffic_blabla [--SWITCH [ARG]]
  void add_global_usage(std::string const &usage);
  // help message that can be called
  void help_message(std::string_view err = "") const;
  /** Require subcommand/options for this command
      @return The Command instance for chained calls.
  */
  Command &require_commands();
  // set the error message
  void set_error(std::string e);
  // get the error message
  std::string get_error() const;

  // Add App's description.
  void add_description(std::string descr);

protected:
  // Converted from 'const char **argv' for the use of parsing and help
  AP_StrVec _argv;
  // the top level command object for program use
  Command _top_level_command;
  // user-customized error message output
  std::string _error_msg;

  friend class Command;
  friend class Arguments;
};

} // namespace ts
