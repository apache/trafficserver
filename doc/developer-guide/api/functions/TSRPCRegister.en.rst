.. Licensed to the Apache Software Foundation (ASF) under one
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

.. include:: ../../../common.defs

.. default-domain:: cpp

TSRPCRegister
*************

Traffic Server JSONRPC method registration.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. type:: TSYaml

   An opaque pointer to an internal representation of a YAML::Node type from yamlcpp library..


.. type:: TSRPCProviderHandle

   An opaque pointer to an internal representation of rpc::RPCRegistryInfo. This object contains context information about the RPC
   handler.

.. type:: TSRPCHandlerOptions

   This class holds information about how a handler will be managed and delivered when called. The JSON-RPC manager would use this
   object to perform certain validation.


   .. var:: bool restricted

      Tells the RPC Manager if the call can be delivered or not based on the config rules.

.. type::  void (*TSRPCMethodCb)(const char *id, TSYaml params);

   JSONRPC callback signature for method.

.. type::  void (*TSRPCNotificationCb)(TSYaml params);

   JSONRPC callback signature for notification.

.. function:: TSRPCProviderHandle TSRPCRegister(const char *provider_name, size_t provider_len, const char *yamlcpp_lib_version, size_t yamlcpp_lib_len);
.. function:: TSReturnCode TSRPCRegisterMethodHandler(const char *name, size_t name_len, TSRPCMethodCb callback, TSRPCProviderHandle info, const TSRPCHandlerOptions *opt);
.. function:: TSReturnCode TSRPCRegisterNotificationHandler(const char *name, size_t name_len, TSRPCNotificationCb callback, TSRPCProviderHandle info, const TSRPCHandlerOptions *opt);
.. function:: TSReturnCode TSRPCHandlerDone(TSYaml resp);
.. function:: void TSRPCHandlerError(int code, const char *descr, size_t descr_len);

Description
===========

:type:`TSRPCRegister` Should be used to accomplish two basic tasks:

#. To perform a ``yamlcpp`` version library validation.
    To avoid binary compatibility issues with some plugins using a different ``yamlcpp`` library version, plugins should
    check-in with TS before registering any handler and validate that their ``yamlcpp`` version is the same as used internally
    in TS.

#. To create the ``TSRPCProviderHandle`` that will be used as a context object for each subsequent handler registration.
    The ``provider_name`` will be used as a part of the service descriptor API(:ref:`get_service_descriptor`) which is available as part of the RPC api.

    .. code-block:: cpp

        TSRPCProviderHandle info = TSRPCRegister("FooBar's Plugin!", 16, "0.8.0", 5);
        ...
        TSRPCHandlerOptions opt{{true}};
        TSRPCRegisterMethodHandler("my_join_string_handler", 22, func, info, &opt);


    Then when requesting :ref:`get_service_descriptor` It will then display as follows:

    .. code-block:: json

        {
            "jsonrpc":"2.0",
            "result":{
            "methods":[
                {
                    "name":"my_join_string_handler",
                    "type":"method",
                    "provider":"FooBar's plugin!",
                    "schema":{ }
                }
            ]
        }


.. note::

   We will provide binary compatibility within the lifespan of a major release.

:arg:`provider_name` should be a string with the Plugin's descriptor. A null terminated string is expected.

:arg:`provider_len` should be the length of the provider string..

:arg:`yamlcpp_lib_version` should be a string with the yaml-cpp library version
the plugin is using. A null terminated string is expected.

:arg:`yamlcpp_lib_len` should be the length of the yamlcpp_lib_len string.

:func:`TSRPCRegisterMethodHandler` Add new registered method handler to the JSON RPC engine.

:arg:`name` call name to be exposed by the RPC Engine, this should match the incoming request.
If you register **get_stats** then the incoming jsonrpc call should have this very
same name in the **method** field. .. {...'method': 'get_stats'...}.

:arg:`name_len` The length of the name string.

:arg:`callback` The function to be registered. Check :type:`TSRPCMethodCb`.

:arg:`info` TSRPCProviderHandle pointer,
this will be used to provide more context information about this call. It is expected to use the one created by ``TSRPCRegister``.

:arg:`opt` Pointer to `TSRPCHandlerOptions`` object. This will be used to store specifics about a particular call, the rpc
manager will use this object to perform certain actions. A copy of this object will be stored by the rpc manager.

Please check :ref:`jsonrpc_development` for examples.

:func:`TSRPCRegisterNotificationHandler` Add new registered method handler to the JSON RPC engine.

:arg:`name` call name to be exposed by the RPC Engine, this should match the incoming request.
If you register **get_stats** then the incoming jsonrpc call should have this very
same name in the **method** field. .. {...'method': 'get_stats'...}.

:arg:`name_len` The length of the name string.

:arg:`callback` The function to be registered. Check :type:`TSRPCNotificationCb`.

:arg:`info` TSRPCProviderHandle pointer,
this will be used to provide more context information about this call. It is expected to use the one created by ``TSRPCRegister``.

:arg:`opt` Pointer to `TSRPCHandlerOptions`` object. This will be used to store specifics about a particular call, the rpc
manager will use this object to perform certain actions. A copy of this object will be stored by the rpc manager.

Please check :ref:`jsonrpc_development` for examples.

:func:`TSRPCHandlerDone` Function to notify the JSONRPC engine that the plugin handler is finished processing the current request.
This function must be used when implementing a 'method' rpc handler. Once the work is done and the
response is ready to be sent back to the client, this function should be called.
Is expected to set the YAML node as response. If the response is empty a **success** message will be
added to the client's response. The :arg:`resp` holds the *YAML::Node* response for this call.


Example:

    .. code-block:: cpp

        void my_join_string_handler(const char *id, TSYaml p) {
            // id = "abcd-id"
            // join string "["abcd", "efgh"]
            std::string join = join_string(p);
            YAML::Node resp;
            resp["join"] = join;

            TSRPCHandlerDone(reinterpret_cast<TSYaml>(&resp));
        }

    This will generate:

    .. code-block:: json

        {
            "jsonrpc":"2.0",
            "result":{
                "join":"abcdefgh"
            },
            "id":"abcd-id"
        }


:func:`TSRPCHandlerError` Function to notify the JSONRPC engine that the plugin handler is finished processing the current request with an error.

:arg:`code` Should be the error number for this particular error.

:arg:`descr` should be a text with a description of the error. It's up to the
developer to specify their own error codes and description. Error will be part of the *data* field in the jsonrpc response. See :ref:`jsonrpc-error`

:arg:`descr_len`` The length of the description string.

Example:

    .. code-block:: cpp

        void my_handler_func(const char *id, TSYaml p) {
            try {
                // some code
            } catch (std::exception const &e) {
                std::string buff;
                swoc::bwprint(buff, "Error during rpc handling: {}.", e.what());
                TSRPCHandlerError(ID_123456, buff.c_str(), buff.size());
                return;
            }
        }

    This will generate:

    .. code-block:: json

        {
            "jsonrpc":"2.0",
            "error":{
                "code":9,
                "message":"Error during execution",
                "data":[
                    {
                        "code":123456,
                        "message":"Error during rpc handling: File not found."
                    }
                ]
            },
            "id":"abcd-id"
        }

.. important::

    You must always inform the RPC after processing the jsonrpc request. Either by calling :func:`TSRPCHandlerDone` or :func:`TSRPCHandlerError`
    . Calling either of these functions twice is a serious error. You should call exactly one of these functions.

Return Values
=============

:func:`TSRPCRegister` returns :enumerator:`TS_SUCCESS` if all is good, :enumerator:`TS_ERROR` if the :arg:`yamlcpp_lib_version`
was not set, or the ``yamlcpp`` version does not match with the one used internally in TS.

:func:`TSRPCRegisterMethodHandler` :enumerator:`TS_SUCCESS` if the handler was successfully registered, :enumerator:`TS_ERROR` if the handler is already registered.

:func:`TSRPCRegisterNotificationHandler`:enumerator:`TS_SUCCESS` if the handler was successfully registered, :enumerator:`TS_ERROR` if the handler is already registered.

:func:`TSRPCHandlerDone` Returns :enumerator:`TS_SUCCESS` if no issues, or  :enumerator:`TS_ERROR` if an issue was found.

:func:`TSRPCHandlerError` Returns :enumerator:`TS_SUCCESS` if no issues, or  :enumerator:`TS_ERROR` if an issue was found.


See also
========

Please check :ref:`jsonrpc_development` for more details on how to use this API.

