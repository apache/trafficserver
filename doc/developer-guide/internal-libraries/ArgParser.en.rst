.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _ArgParser:

ArgParser
*********

Synopsis
++++++++

.. code-block:: cpp

   #include <ts/ArgParser.h>

Description
+++++++++++

:class:`ArgParser` is a powerful and easy-to-use command line Parsing library for ATS.
The program defines what it requires by adding commands and options.
Then :class:`ArgParser` will figure out the related information from the command line.
All parsed arguments and function will be put in a key-value pairs structure
:class:`Arguments` available for users to use.

Usage
+++++

The usage of the ArgParser is straightforward. The user is expected to create an
ArgParser for the program. Commands and options can be added to the parser with details
including ENV variable, arguments expected, etc. After a single method :code:`parse(argv)` is called,
An object containing all information and parsed arguments available to use will be returned.

Create a parser
---------------
The first step to use ArgParser is to create a parser.
The parser can be created simply by calling:

.. code-block:: cpp

   ts::ArgParser parser;

or initialize with the following arguments: 
*name, help description, environment variable, argument number expected, function*

.. code-block:: cpp

   ts::ArgParser parser("name", "description", "ENV_VAR", 0, &function);

To add the usage for the help message of this program:

.. code-block:: cpp

   parser.add_global_usage("traffic_blabla [--SWITCH]");

Add commands and options
------------------------

We can perform all kinds of operations on the parser which is the :class:`ArgParser` or command which is a :class:`Command`.

To add commands to the program or current command:

.. code-block:: cpp

   ts::ArgParser &command1 = parser.add_command("command1", "description");
   command1.add_command("command2", "description");
   command1.add_command("command3", "description", "ENV_VAR", 0);
   command1.add_command("command4", "description", "ENV_VAR", 0, &function, "lookup_key");

This function call returns the new :class:`Command` instance added.

.. Note::

   The 0 here is the number of arguments we expected. It can be also set to `MORE_THAN_ZERO_ARG_N` or `MORE_THAN_ONE_ARG_N`
   to specify that this command expects all the arguments come later (more than zero or more than one).

To add options to the parser or current command:

.. code-block:: cpp

    parser.add_option("--switch", "-s", "switch description");
    command1.add_option("--switch", "-s", "switch description", "", 0);
    command1.add_option("--switch", "-s", "switch description", "ENV_VAR", 1, "default", "lookup_key");

This function call returns the new :class:`Option` instance. (0 is also number of arguments expected)

We can also use the following chained way to add subcommand or option:

.. code-block:: cpp

    command.add_command("init", "description").add_command("subinit", "description");
    command.add_command("remove", "description").add_option("-i", "--initoption");

which is equivalent to

.. code-block:: cpp

    ts::ArgParser::Command &init_command = command.add_command("init", "description");
    ts::ArgParser::Command &remove_command = command.add_command("remove", "description");
    init_command.add_command("subinit", "description");
    remove_command.add_option("-i", "--initoption");

In this case, `subinit` is the subcommand of `init` and `--initoption` is a switch of command `remove`.


Parsing Arguments
-----------------

:class:`ArgParser` parses arguments through the :code:`parse(argv)` method. This will inspect the command line
and walk through it. A :class:`Arguments` object will be built up from attributes
parsed out of the command line holding key-value pairs all the parsed data and the function.

.. code-block:: cpp

    Arguments args = parser.parse(argv);

Invoke functions
----------------

To invoke the function associated with certain commands, we can perform this by simply calling :code:`invoke()`
from the :class:`Arguments` object returned from the parsing. The function can be a lambda.

.. code-block:: cpp

    args.invoke();

Help and Version messages
-------------------------

- Help message will be outputted when a wrong usage of the program is detected or `--help` option found.

- Version message is defined unified in :code:`ArgParser::version_message()`.

Classes
+++++++

.. class:: ArgParser

   .. function:: Option &add_option(std::string const &long_option, std::string const &short_option, std::string const &description, std::string const &envvar = "", unsigned arg_num = 0, std::string const &default_value = "", std::string const &key = "")

      Add an option to current command with *long name*, *short name*, *help description*, *environment variable*, *arguments expected*, *default value* and *lookup key*. Return The Option object itself.

   .. function:: Command &add_command(std::string const &cmd_name, std::string const &cmd_description, std::function<void()> const &f = nullptr, std::string const &key = "")

      Add a command with only *name* and *description*, *function to invoke* and *lookup key*. Return the new :class:`Command` object.

   .. function:: Command &add_command(std::string const &cmd_name, std::string const &cmd_description, std::string const &cmd_envvar, unsigned cmd_arg_num, std::function<void()> const &f = nullptr, std::string const &key = "")

      Add a command with *name*, *description*, *environment variable*, *number of arguments expected*, *function to invoke* and *lookup key*.
      The function can be passed by reference or be a lambda. It returns the new :class:`Command` object.

   .. function:: void parse(const char **argv)

      Parse the command line by calling :code:`parser.parse(argv)`. Return the new :class:`Arguments` instance.

   .. function:: void help_message() const

      Output usage to the console.

   .. function:: void version_message() const

      Output version string to the console.

   .. function:: void add_global_usage(std::string const &usage)

      Add a global_usage for :code:`help_message()`. Example: `traffic_blabla [--SWITCH [ARG]]`.

   .. function:: Command &require_commands()

      Make the parser require commands. If no command is found, output help message. Return The :class:`Command` instance for chained calls.

   .. function:: void set_error(std::string e)

      Set the user customized error message for the parser.

   .. function:: std::string get_error() const

      Return the error message of the parser.

.. class:: Option

   :class:`Option` is a data struct containing information about an option.

.. code-block:: cpp

   struct Option {
      std::string long_option;   // long option: --arg
      std::string short_option;  // short option: -a
      std::string description;   // help description
      std::string envvar;        // stored ENV variable
      unsigned arg_num;          // number of argument expected
      std::string default_value; // default value of option
      std::string key;           // look-up key
   };

.. class:: Command

   :class:`Command` is a nested structure of command for :class:`ArgParser`. The :code:`add_option()`, :code:`add_command()` and
   :code:`require_command()` are the same with those in :class:`ArgParser`. When :code:`add_command()`
   is called under certain command, it will be added as a subcommand for the current command. For Example, :code:`command1.add_command("command2", "description")`
   will make :code:`command2` a subcommand of :code:`command1`. :code:`require_commands()` is also available within :class:`Command`.

   .. function:: void add_example_usage(std::string const &usage)

      Add an example usage for the command to output in `help_message`.
      For Example: :code:`command.add_example_usage("traffic_blabla init --path=/path/to/file")`.

.. class:: Arguments

   :class:`Arguments` holds the parsed arguments and function to invoke.
   It basically contains a function to invoke and a private map holding key-value pairs.
   The key is the command or option name string and the value is the Parsed data object which
   contains the environment variable and arguments that belong to this certain command or option.

   .. function:: std::string get(std::string const &name)

      Return the :class:`ArgumentData` object related to the name.

   .. function:: std::string set_env(std::string const &key, std::string const &value)

      Set the environment variable given `key`.

   .. function:: void append(std::string const &key, ArgumentData const &value)

      Append key-value pairs to the map in :class:`Arguments`.

   .. function:: void append_arg(std::string const &key, std::string const &value)

      Append `value` to the data of `key`.

   .. function:: void show_all_configuration() const

      Show all the called commands, options, and associated arguments.

   .. function:: void invoke()

      Invoke the function associated with the parsed command.

   .. function:: void has_action() const

      return true if there is any function to invoke.

.. class:: ArgumentData

   :class:`ArgumentData` is a struct containing the parsed Environment variable and command line arguments.
   There are methods to get the data out of it.

   Note: More methods can be added later to get specific types from the arguments.

   .. function:: operator bool() const noexcept

      `bool` for checking if certain command or option is called.

   .. function:: std::string const &operator[](int x) const

      Index accessing operator.

   .. function:: std::string const &env() const noexcept

      Return the environment variable associated with the argument.

   .. function:: std::vector<std::string>::const_iterator begin() const noexcept

      Begin iterator for iterating the arguments data.

   .. function:: std::vector<std::string>::const_iterator end() const noexcept

      End iterator for iterating the arguments data.

   .. function:: std::string const &at(unsigned index) const

      Index accessing method.

   .. function:: std::string const &value() const noexcept

      Return the first element of the arguments data.

   .. function:: size_t size() const noexcept

      Return the size of the arguments vector

   .. function:: size_t empty() const noexcept

      Return true if the arguments vector and env variable are both empty.

Example
+++++++

ArgParser
---------

Below is a short example of using the ArgParser. We add some options and some commands to it using different ways.
This program will have such functionality:

- ``--switch``, ``-s`` as a global switch.
- Command ``func`` will call ``function()`` and this command takes 2 argument.
- Command ``func2`` will call ``function2(int num)``.
- Command ``init`` has subcommand ``subinit`` and option ``--path`` which take 1 argument.
- Command ``remove`` has option ``--path`` which takes 1 argument and has ``HOME`` as the environment variable.

.. code-block:: cpp

    #include "ts/ArgParser.h"

    void function() {
        ...
    }

    void function2(int num) {
        ...
    }

    int main (int, const char **argv) {
        ts::ArgParser parser;
        parser.add_global_usage("traffic_blabla [some stuff]");

        parser.add_option("--switch", "-s", "top level switch");
        parser.add_command("func", "some function", "ENV_VAR", 2, &function);
        parser.add_command("func2", "some function2", [&]() { return function2(100); });

        auto &init_command = parser.add_command("init", "initialize");
        init_command.add_option("--path", "-p", "specify the path", "", 1);
        init_command.add_command("subinit", "sub initialize");

        parser.add_command("remove", "remove things").add_option("--path", "-p", "specify the path", "HOME", 1);
        
        ts::Arguments parsed_data = parser.parse(argv);
        parsed_data.invoke();
        ...
    }

Arguments
---------
To get the values from the arguments data, please refer to the methods in :class:`Arguments` and :class:`ArgumentData`
