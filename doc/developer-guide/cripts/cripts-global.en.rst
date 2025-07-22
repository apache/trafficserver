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

.. _cripts-global:

Cripts for global plugin
************************

In addition to being a scripting language for per-property, or remap rules, Cripts can
also be used to write global ATS plugins. This is a more advanced use case, and requires
some knowledge of how ATS works.

The automatic building of plugins that Cripts supports as a remap rule is not available
(yet) to global plugins. As such, you must compile any global plugin manually. You can
see this as an alternative to writing a plugin in regular C++, in fact you can still
combine a global plugin with a remap rule plugin in the same Cript file.

Usage
=====

As with remap rules, global Cripts still requires both the preamble as well as the epilogue.
However, all callbacks are prefixed with ``glb_`` to indicate that they are global hooks.
See the hooks below for more details.

Global plugins must be compiled manually and loaded through the standard ATS plugin
configuration. Here's a complete example:

.. code-block:: cpp

   #include <cripts/Preamble.hpp>

   glb_init()
   {
     CDebug("Global Cript plugin initialized");
   }

   glb_read_request()
   {
     borrow url = cripts::Client::URL::Get();
     borrow req = cripts::Client::Request::Get();

     // Example: Add a custom header to all requests
     req["X-Global-Plugin"] = "active";

     // Example: Filter query parameters globally
     url.query.Keep({"foo", "bar"});
   }

   glb_send_response()
   {
     borrow resp = cripts::Client::Response::Get();

     // Example: Add response header to all responses
     resp["X-Processed-By"] = "Cripts-Global";
   }

   #include <cripts/Epilogue.hpp>

Hooks
=====

Hooks are the main way to interact with ATS. The hooks are the same as the ATS hooks,
but with a few differences. The hooks are all in the global namespace, and the hooks
are all functions. Cripts provides a core set of hooks which are always available,
but they are not required to be used.

Not all ATS hooks are available in Cripts, but the most common ones are. Hooks are
implicitly called if they are defined in the Cript file. The Cript will never explicitly
setup the hooks, as this is done by the ATS process.

Normal Hooks
------------

Let's look at the normal hooks that are available in Cripts. Note here that the name
of the function dictates the underlying ATS hook.

.. _cripts-global-hooks-init:

glb_init()
^^^^^^^^^^

This callback is called when the plugin is loaded. This is where you can setup any
global state that you need, initialize metrics, or perform other one-time setup tasks.

Example:

.. code-block:: cpp

   glb_init()
   {
     CDebug("Initializing global Cript plugin");
     // Setup global metrics, configurations, etc.
   }

.. _cripts-global-hooks-txn-start:

glb_txn_start()
^^^^^^^^^^^^^^^

The ``glb_txn_start()`` hook is called at the beginning of a transaction. This is also
where Cripts will setup other HTTP hooks as necessary. Note that in this hook, the
client request has not yet been read, so you cannot access the request headers.

This hook is useful for initializing per-transaction data or setting up transaction-specific
state.

Example:

.. code-block:: cpp

   glb_txn_start()
   {
     // Initialize transaction data
     txn_data[0] = integer(0); // Request counter
     txn_data[1] = cripts::Time::Local::Now().Epoch();
   }

.. _cripts-global-hooks-read-request:

glb_read_request()
^^^^^^^^^^^^^^^^^^

The ``glb_read_request()`` hook is called after the client request has been read. This
means that you can access the request headers, and the request URL. However, remap rules
have not yet been applied, so the URL may not be the final URL, or even complete.

This is the earliest point where you can examine and modify the client request.

Example:

.. code-block:: cpp

   glb_read_request()
   {
     borrow req = cripts::Client::Request::Get();
     borrow url = cripts::Client::URL::Get();

     // Log all incoming requests
     CDebug("Request: {} {}", req.method, url.path);

     // Add tracking headers
     req["X-Request-ID"] = cripts::UUID::Request::Get();
   }

.. _cripts-global-hooks-pre-remap:

glb_pre_remap()
^^^^^^^^^^^^^^^

The ``glb_pre_remap()`` hook is called just before remap rules are processed. This hook
may not be particularly useful in most Cripts, as remap rules have not yet been applied.
We've added it for completeness and advanced use cases.

.. _cripts-global-hooks-post-remap:

glb_post_remap()
^^^^^^^^^^^^^^^^

The ``glb_post_remap()`` hook is called after remap rules have been applied. This is the
closest equivalent to the ``do_remap()`` hook in remap rules, but operates globally
across all transactions.

At this point, the request URL has been finalized and you can make decisions based on
the final destination.

Example:

.. code-block:: cpp

   glb_post_remap()
   {
     borrow url = cripts::Client::URL::Get();

     // Apply global policies after remap
     if (url.host == "sensitive.example.com") {
       borrow req = cripts::Client::Request::Get();
       req["X-Security-Level"] = "high";
     }
   }

.. _cripts-global-hooks-cache-lookup:

glb_cache_lookup()
^^^^^^^^^^^^^^^^^^

The ``glb_cache_lookup()`` hook is called when a cache lookup is performed. This allows
you to take action based on whether the content was found in cache or not, and to
modify cache behavior globally.

This is equivalent to the ``do_cache_lookup()`` hook in remap rules.

Example:

.. code-block:: cpp

   glb_cache_lookup()
   {
     auto status = transaction.LookupStatus();
     CDebug("Cache lookup status: {}", static_cast<int>(status));

     // Global cache policy decisions can be made here
   }

.. _cripts-global-hooks-send-request:

glb_send_request()
^^^^^^^^^^^^^^^^^^

The ``glb_send_request()`` hook is called when the request is ready to be sent to the
origin server. This is the main hook for globally modifying requests to origin servers.

This is equivalent to the ``do_send_request()`` hook in remap rules.

Example:

.. code-block:: cpp

   glb_send_request()
   {
     borrow req = cripts::Server::Request::Get();

     // Add global origin request headers
     req["X-Forwarded-By"] = "ATS-Cripts";
     req["X-Request-Time"] = cripts::Time::Local::Now().Epoch();
   }

.. _cripts-global-hooks-read-response:

glb_read_response()
^^^^^^^^^^^^^^^^^^^

The ``glb_read_response()`` hook is called when the response is being read from the origin
server. This is the main hook for globally modifying responses from origin servers before
they are cached or sent to clients.

This is equivalent to the ``do_read_response()`` hook in remap rules.

Example:

.. code-block:: cpp

   glb_read_response()
   {
     borrow resp = cripts::Server::Response::Get();

     // Global response processing
     if (resp.status == 500) {
       CDebug("Server error detected from origin");
       // Could implement global error handling here
     }
   }

.. _cripts-global-hooks-send-response:

glb_send_response()
^^^^^^^^^^^^^^^^^^^

The ``glb_send_response()`` hook is called when the response is ready to be sent to the
client. This allows you to make final modifications to the response headers and implement
global response policies.

This is equivalent to the ``do_send_response()`` hook in remap rules.

Example:

.. code-block:: cpp

   glb_send_response()
   {
     borrow resp = cripts::Client::Response::Get();

     // Add global response headers
     resp["X-Served-By"] = "ATS-Cripts-Global";
     resp["X-Response-Time"] = cripts::Time::Local::Now().Epoch();

     // Global security headers
     resp["X-Content-Type-Options"] = "nosniff";
     resp["X-Frame-Options"] = "DENY";
   }

.. _cripts-global-hooks-txn-close:

glb_txn_close()
^^^^^^^^^^^^^^^

The ``glb_txn_close()`` hook is called when the transaction is closed. This is useful
for cleanup, logging, and metrics collection.

This is equivalent to the ``do_txn_close()`` hook in remap rules.

Example:

.. code-block:: cpp

   glb_txn_close()
   {
     // Log transaction completion
     auto start_time = AsInteger(txn_data[1]);
     auto duration = cripts::Time::Local::Now().Epoch() - start_time;

     CDebug("Transaction completed in {} seconds", duration);

     // Update global metrics
     // Cleanup transaction-specific resources
   }

Hook Execution Order
====================

The global hooks are executed in the following order during a typical transaction:

1. ``glb_init()`` - Called once when plugin loads
2. ``glb_txn_start()`` - Transaction begins
3. ``glb_read_request()`` - Client request is read
4. ``glb_pre_remap()`` - Before remap processing
5. ``glb_post_remap()`` - After remap processing
6. ``glb_cache_lookup()`` - Cache lookup performed
7. ``glb_send_request()`` - Request sent to origin (if cache miss)
8. ``glb_read_response()`` - Response read from origin
9. ``glb_send_response()`` - Response sent to client
10. ``glb_txn_close()`` - Transaction cleanup

.. note::
    Not all hooks are called for every transaction. For example, ``glb_send_request()``
    and ``glb_read_response()`` are only called on cache misses when ATS needs to
    contact the origin server.

Best Practices
==============

When writing global Cripts plugins:

1. **Performance Considerations**: Global hooks affect all traffic, so keep processing
   lightweight and efficient.

2. **Conditional Logic**: Use conditional logic to apply policies only where needed:

   .. code-block:: cpp

      glb_read_request()
      {
        borrow url = cripts::Client::URL::Get();

        // Only process specific hosts
        if (url.host.find("api.") == 0) {
          // API-specific processing
        }
      }

3. **Error Handling**: Implement proper error handling to avoid affecting other traffic:

   .. code-block:: cpp

      glb_send_response()
      {
        if (something_went_wrong) {
          CDebug("Error in global response processing");
          // Don't let errors affect the response
        } else {
        // Continue with normal processing
          borrow resp = cripts::Client::Response::Get();
          resp["X-Processed-By"] = "Cripts-Global";
        }
      }

4. **Metrics and Monitoring**: Use global plugins for comprehensive monitoring:

   .. code-block:: cpp

      glb_init()
      {
        instance.metrics[0] = cripts::Metrics::Counter::Create("global.requests.total");
        instance.metrics[1] = cripts::Metrics::Counter::Create("global.errors.total");
      }

5. **Resource Management**: Clean up any resources in ``glb_txn_close()`` to prevent leaks.
