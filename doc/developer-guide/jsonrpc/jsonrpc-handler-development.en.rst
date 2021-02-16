.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. |RPC| replace:: ``JSONRPC 2.0``

.. _JSONRPC: https://www.jsonrpc.org/specification
.. _JSON: https://www.json.org/json-en.html
.. _YAML: https://github.com/jbeder/yaml-cpp/wiki/Tutorial

.. _jsonrpc_development:

JSONRPC handler implementation
******************************

Use this section as a guide for developing new rpc methods inside |TS| and how to expose them through the |RPC| endpoint.
Before we start, it is worth mentioning some of the architecture of the current implementation. The whole RPC mechanism is divided in
few components.

Json RPC manager
================

This class is the entrance point for both, server calls and registered functions.

.. figure:: ../../uml/images/JsonRPCManager.svg

Dispatcher class
^^^^^^^^^^^^^^^^^

* Class that keeps track of all the registered methods and notifications that can be invoked by the RPC server. This class holds two
  hash tables containing methods and notifications which uses the method name as a key.
* This class internally consumes ``RPCRequestInfo`` objects and performs the invocation of the respective calls.
* This class handlers the responses from the registered callbacks and it fills the appropriated ``RPCResponseInfo`` which then is passed
  back to the ``JsonRPCManager`` class.


JsonRPCManager class
^^^^^^^^^^^^^^^^^^^^

* Singleton class that handles the JSONRPC handler registration and JSONRPC handler invocation.
* This class is the main entrance point for the RPC server through ``handle_call(std::string_view req)`` function.
* This class is the main entrance point for handler to be able to register in the RPC logic. ``add_notification_handler`` and ``remove_notification_handler``.


Implementing new handlers
=========================

There a a few basic concepts that needs to be known before implementing a new handler, this is an easy process and the complexity depends on
the nature of the handler that you want to implement.
Dealing with incoming and outgoing parameters is up to the developer, we will touch some ways to deal with this through this guide.

.. _jsonrpc_development-design:

Design
^^^^^^

As requirement from the ``JsonRPCManager`` in order to be able to register inside the RPC management a function should implement the
following signature:

Methods:

.. code-block:: cpp

    ts::Rv<YAML::Node> your_rpc_handler_function_name(std::string_view const &id, YAML::Node const &params);



Notifications:

.. code-block:: cpp

    void your_rpc_handler_function_name(YAML::Node const &params);


* Incoming method request's id will be passed to the handler, this is read only value as the server is expected to respond with the same value.
* ``YAML::Node`` params is expected to be a ``Sequence`` or a ``Map``, as per protocol this cannot be a single value, so do not expect things like:
  ``param=123`` or ``param=SomeString``.
* The ``params`` can be empty and contains no data at all.


.. note::

    ``method`` handlers are expected to respond to the requests, while ``notifications``` will not respond with any data nor error. You can find
    more information in :ref:`jsonrpc-protocol` or directly in the protocol specs `JSONRPC`_.


.. note::

    If there is no explicit response from the method, the protocol implementation will respond with :ref:`success_response` unless an error
    was specified.


Error Handling
^^^^^^^^^^^^^^


There are several ways to deal with internal handler errors. Errors are expected to be sent back to the client if the API was expressed that way
and if the request was a ``method``.
We have defined some generic errors that can be used to respond depending on the nature of the registered handler,
please check :ref:`jsonrpc-handler-errors` for more info.

We recommend some ways to deal with this:

#. Using the ``Errata`` from ``ts::Rv<YAML::Node>``

This can be set in case you would like to let the server to respond with an |RPC| error, ``ExecutionError`` will be used to catch all the
errors that are fired from within the function call, either by setting the proper errata or by throwing an exception.
Please refer to `rpc-error-code`_ and in particular ``ExecutionError = 9``. Also check :ref:`jsonrpc-handler-errors`

.. note::

    Errors have preference over any other response, so if you set both, the errata and the ``YAML::Node`` response, then the former
    will be set in the response.

#. Defining a custom error object and include this as a part of the response object.

* This is up to the developer and the errors can be part of the response ``YAML::Node``.
* The JSONRPC Dispatcher will read that there is no error returned from the call and use the result to build the response. If this is
  what you are willing to respond, then make sure that the error is not set in the ``ts::Rv``.



Examples:

* Respond with an error, no ``result`` field will be set in the response.

    .. code-block:: cpp

        ts::Rv<YAML::Node>
        respond_with_an_error(std::string_view const &id, YAML::Node const &params)
        {
            using namespace rpc::handlers::errors;
            return make_errata(Codes::SERVER, "Something happened in the server");
        }

    Server's response.

    .. code-block:: json

        {
            "jsonrpc":"2.0",
            "error":{
                "code":9,
                "message":"Error during execution",
                "data":[
                    {
                        "code":3000,
                        "message":"Something happened in the server"
                    }
                ]
            },
            "id":"abcd-id"
        }

    .. note::

        ``make_errata`` hides some internal details when creating an errata.

* Response with custom handler error. In this case, make sure that the API definition and documentation reflects this as so far we do not
  have json schemas to enforce any of this on the client side.


   .. code-block:: cpp

      ts::Rv<YAML::Node>
      respond_with_my_own_error(std::string_view const &id, YAML::Node const &params)
      {
          YAML::Node resp;
          resp["HandlerErrorDescription"] = "I can set up my own error in the result field.";
          return resp;
      }

    The "error" is part of the ``result``, in this case this could be used as any other field, the example would be the same.

   .. code-block:: json

      {
        "jsonrpc":"2.0",
        "result":{
            "HandlerErrorDescription":"I can set up my own error in the result field."
        },
        "id":"abcd-id"
      }


We have selected the ``ts::Rv<YAML::Node>`` as a message interface as this can hold the actual response/error.



Registration
^^^^^^^^^^^^

Handler registration should be done by using the ``JsonRPCManager`` singleton object. Note that there are a set of convenient helper
functions that can be used to achieve registration through the singleton object.

.. code-block:: cpp

    namespace rpc {
        // this set of functions will call the singleton object and perform the same as by using the singleton directly.
        add_handler(...)
        add_notification_handler(...)
    }


.. code-block:: cpp

    // Handler implementation
    ts::Rv<YAML::Node>
    my_handler_impl(std::string_view const &id, YAML::Node const &params)
    {
        using namespace rpc::handlers::errors;
        return make_errata(Codes::SERVER, "Something happened in the server");
    }

The actual registration:

.. code-block:: cpp

    #include "rpc/jsonrpc/JsonRPCManager.h"
    ...
    rpc::add_handler("my_handler_impl", &my_handler_impl);


In case of a need to remove a handler from the rpc mechanism, this can also be done, by just using the remove* version of the same
functions as used to register.


Notification example:

As mentioned before, notifications does not need to respond, as they are "fire and forget" calls, no id will be provided as part of the api.

.. code-block:: cpp

    void
    my_notification_handler(YAML::Node const &params)
    {
        // do something
        // all errors will be ignored by the server.
    }

Registration for notifications uses a different API:

.. code-block:: cpp

    #include "rpc/jsonrpc/JsonRPCManager.h"
    rpc::JsonRPCManager::instance().add_notification_handler("admin_server_restart", &server_shutdown);


Testing the API using autest framework
======================================

TBC


Important Notes
===============

* You can refer to `YAML`_ for more info in how code/decode yaml content.
* Remember to update :ref:`admin-jsonrpc` if you are adding a new handler.
* If a new handler needs to be exposed through :program:`traffic_ctl` please refer to :ref:`traffic_ctl_jsonrpc` for a general idea
  and to :ref:`developer-guide-traffic_ctl-development` for how to implement a new command.


:ref:`admnin-jsonrpc-configuration`,
:ref:`jsonrpc-protocol`
:ref:`developer-guide-traffic_ctl-development`


