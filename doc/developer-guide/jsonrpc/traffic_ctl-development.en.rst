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

.. |RPC| replace:: JSONRPC 2.0

.. _YAML: https://github.com/jbeder/yaml-cpp/wiki/Tutorial

.. _developer-guide-traffic_ctl-development:

Traffic Control Development Guide
*********************************

Traffic Control interacts with |TS| through the |RPC| endpoint. All interaction is done by following the |RPC| protocol.

Overall structure
=================

.. figure:: ../../uml/images/traffic_ctl-class-diagram.svg


* The whole point is to separate the command handling from the printing part.
* Printing should be done by an appropriate Printer implementation, this should support several kinds of printing formats.
* For now, everything is printing in the standard output, but a particular printer can be implemented in such way that
  the output could be sent to a different destination.
* JSONRPC requests have a base class that hides some of the basic and common parts, like ``id``, and ``version``. When deriving
  from this class, the only thing that needs to be override is the ``method``


.. important::

   CtrlCommand will invoke ``_invoked_func`` when executed, this should be set by the derived class

* The whole design is that the command will execute the ``_invoked_func`` once invoked. This function ptr should be set by the
  appropriated derived class based on the passed parameters. The derived class have the option to override the execute() which
  is a ``virtual`` function and does something else. Check ``RecordCommand`` as an example.


Command implementation
======================

#. Add the right command to the ``ArgParser`` object inside the ``traffic_ctl.cc``.
#. If needed, define a new ``Command`` derived class inside the ``CtrlCommands`` file. if it's not an new command level, and it's a subcommand,
   then you should check the existing command to decide where to place it.

      * Implement the member function that will be dealing with the particular command, ie: (config_status())

      * If a new JsonRPC Message needs to be done, then implement it by deriving from ``shared::rpc::ClientRequest`` if a method is needed, or from
        ``shared::rpc::ClientRequestNotification`` if it's a notification.         More info can be found here :ref:`jsonrpc-request` and
        :ref:`jsonrpc_development-design`. This can be done inside the ``RPCRequest.h`` file.


      .. note::

         Make sure you override the ``std::string get_method() const`` member function with the appropriate api method name.


#. If needed define a new ``Printer`` derived class inside  the ``CtrlPrinter`` file.

   * If pretty printing format will be supported, then make sure you read the ``_format`` member you get from the ``BasePrinter`` class.

#. If it's a new command level (like config, metric, etc), make sure you update the ``Command`` creation inside the ``traffic_ctl.cc`` file.

Implementation Example
======================

Let's define a new command for a new specific API with name == ``admin_new_command_1`` with the following json structure:

.. code-block: json

   {
      "id":"0f0780a5-0758-4f51-a177-752facc7c0eb",
      "jsonrpc":"2.0",
      "method":"admin_new_command_1",
      "params":{
         "named_var_1":"Some value here"
      }
   }


.. code-block:: bash

   $ traffic_ctl new-command new-subcommand1

#. Update ``traffic_ctl.cc``. I will ignore the details as they are trivial.

#. Define a new Request.

   So based on the above json, we can model our request as:

   .. code-block:: cpp

      // RPCRequests.h
      struct NewCommandJsonRPCRequest : shared::rpc::ClientRequest {
         using super = shared::rpc::ClientRequest;
         struct Params {
            std::string named_var_1;
         };
         NewCommandJsonRPCRequest(Params p)
         {
            super::params = p; // This will invoke the built-in conversion mechanism in the yamlcpp library.
         }
         // Important to override this function, as this is the only way that the "method" field will be set.
         std::string
         get_method() const {
            return "admin_new_command_1";
         }
      };

#. Implement the yamlcpp convert function, Yaml-cpp has a built-in conversion mechanism. You can refer to `YAML`_ for more info.

   .. code-block:: cpp

      // yaml_codecs.h
      template <> struct convert<NewCommandJsonRPCRequest::Params> {
         static Node
         encode(NewCommandJsonRPCRequest::Params const &params)
         {
            Node node;
            node["named_var_1"] = params.named_var_1;
            return node;
         }
      };

#. Define a new command. For the sake of simplicity I'll only implement it in the ``.h`` files.

   .. code-block:: cpp

      // CtrlCommands.h & CtrlCommands.cc
      struct NewCommand : public CtrlCommand {
         NewCommand(ts::Arguments args): CtrlCommand(args)
         {
            // we are interested in the format.
            auto fmt = parse_format(_arguments);
            if (args.get("new-subcommand1") {
               // we need to create the right printer.
               _printer = std::make_sharec<MyNewSubcommandPrinter>(fmt);
               // we need to set the _invoked_func that will be called when execute() is called.
               _invoked_func = [&]() { handle_new_subcommand1(); };
            }
            // if more subcommands are needed, then add them here.
         }

      private:
         void handle_new_subcommand1() {
            NewCommandJsonRPCRequest req{};
            // fill the req if needed.
            auto response = invoke_rpc(req);
            _printer->write_output(response);
         }
      };

#. Define a new printer to deal with this command. We will assume that the printing will be different for every subcommand.
   so we will create our own one.


   .. code-block:: cpp

      class MyNewSubcommandPrinter : public BasePrinter
      {
         void write_output(YAML::Node const &result) override {
            // result will contain what's coming back from the server.
         }
      };

   In case that the format type is important, then we should allow it by accepting the format being passed in the constructor.
   And let it set the base one as well.

   .. code-block:: cpp

      MyNewSubcommandPrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}



   The way you print and the destination of the message is up to the developer's needs, either a terminal or some other place. If the response
   from the server is a complex object, you can always model the response with your own type and use the built-in yamlcpp mechanism
   to decode the ``YAML::Node``.  ``write_output(YAML::Node const &result)`` will only have the result defined in the protocol,
   check :ref:`jsonrpc-result` for more detail. So something like this can be easily achieved:

   .. code-block:: cpp

         void
         GetHostStatusPrinter::write_output(YAML::Node const &result)
         {
            auto response = result.as<NewCommandJsonRPCRResponse>(); // will invoke the yamlcpp decode.
            // you can now deal with the Record object and not with the yaml node.
         }

Notes
=====

There is code that was written in this way by design, ``RecordPrinter`` and ``RecordRequest`` are meant to be use by any command
that needs to query and print records without any major hassle.



:ref:`admnin-jsonrpc-configuration`,
:ref:`traffic_ctl_jsonrpc`,
:ref:`jsonrpc_development`
