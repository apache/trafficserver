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

In addition to be a scripting language for per-property, or remap rules, Cripts can
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
See the hooks below for more details. Example:

The third step is to put the Cript file in either the configuration or plugin installation,
depending on your preference. The file must be readable by the ATS process. Example:

.. code-block:: cpp

   #include <cripts/Preamble.hpp>

   glb_read_request()
   {
     borrow url = cripts::Client::URL::Get();

     url.query.Keep({"foo", "bar"});
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

Lets look at the normal hooks that are available in Cripts. Note here that the name
of the function dictates the underlying ATS hook.

.. _cripts-global-hooks-txn-start:

glb_txn_start()
^^^^^^^^^^^^^^^

The ``glb_txn_start()`` hook is called at the beginning of a transaction. This is also
where Cripts will setup other HTTP hooks as necessary. Note that in this hook, the
client request has not yet been read, so you cannot access the request headers.

.. _cripts-global-hooks-read-request:

glb_read_request()
^^^^^^^^^^^^^^^^^^

The ``glb_read_request()`` hook is called after the client request has been read. This
means that you can access the request headers, and the request URL. However, remap rules
has not yet been applied, so the URL may not be the final URL, or even complete.

.. _cripts-global-hooks-pre-remap:

glb_pre_remap()
^^^^^^^^^^^^^^^

This hook may not be particularly useful in Cripts, as remap rules are not applied here yet
as well. We've added it for completeness.

.. _cripts-global-hooks-post-remap:

glb_post_remap()
^^^^^^^^^^^^^^^^

The ``glb_post_remap()`` hook is called after remap rules have been applied. This is the
closest to the ``do_remap()`` hook in remap rules.

.. _cripts-global-hooks-cache_lookup:

glb_cache_lookup()
^^^^^^^^^^^^^^^^^^

This is identical to the :ref:`cripts-overview-hooks-do-cache-lookup` hook in remap rules.

.. _cripts-global-hooks-send-request:

glb_send_request()
^^^^^^^^^^^^^^^^^^

This is identical to the :ref:`cripts-overview-hooks-do-send-request` hook in remap rules.

.. _cripts-global-hooks-read_response:

glb_read_response()
^^^^^^^^^^^^^^^^^^^

This is identical to the :ref:`cripts-overview-hooks-do-read-response` hook in remap rules.

.. _cripts-global-hooks-send_response:

glb_send_response()
^^^^^^^^^^^^^^^^^^^

This is identical to the :ref:`cripts-overview-hooks-do-send-response` hook in remap rules.

.. _cripts-global-hooks-txn-close:

glb_txn_close()
^^^^^^^^^^^^^^^

This is identical to the :ref:`cripts-overview-hooks-do-txn-close` hook in remap rules.

.. _cripts-global-hooks-init:

glb_init()
^^^^^^^^^^

This callback is called when the plugin is loaded. This is where you can setup any
global state that you need etc.
