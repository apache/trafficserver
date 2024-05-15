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

.. _cripts-overview:

Overview
********

Cripts, *C-Scripts*, is a set of wrappers and include files for making simple ATS
plugins easy to generate, modify or create from scratch. A key design here is that
the Code is the Configuration, i.e. the intent really is to have a custom Cript file
for every remap rule in the running system.

Cripts can also be seen as the scripting configuration language for many custom
ATS setups. ATS can even load Cripts directly, if so desired. See the section on
using Cripts for more information.

.. _cripts-overview-building:

Building
========

Cripts is built as a separate library, which can be enabled with ``-DENABLE_CRIPTS=ON``
when configuring *cmake*. Building a Cript itself is just like compiling a normal
ATS plugin, except it will also need the Cripts library linked in: ``-lcripts``.

Plugins built using this method loads exactly like any other ATS plugin, and can be
used in the same way. The only difference is that the plugin will be a lot simpler
to write and maintain.

.. _cripts-overview-usage:

Usage
=====

The easiest way to use Cripts is to let ATS itself compile and load the Cript. This
requires three things:

1. The :file:`records.yaml` must have the ``plugin.compiler_path`` set to a build script.
2. The remap rule must specify the Cript file to load.
3. The Cript file must be in either the configuration or plugin installation directory.

The first step is to set the plugin.compiler_path in the records.yaml file. For example:

.. code-block:: yaml

   ts:
     plugin:
       compiler_path: /opt/ats/bin/compiler.sh

The second step is to specify the Cript file in the remap rule. For example:

::

    map https://www.example.com https://origin.example.com \
        @plugin=example.cript

The third step is to put the Cript file in either the configuration or plugin installation,
depending on your preference. The file must be readable by the ATS process. Example:

.. code-block:: cpp

   #include <cripts/Preamble.hpp>

   do_remap()
   {
     borrow url = Client::URL::get();

     url.query.keep({"foo", "bar"});
   }

   #include <cripts/Epilogue.hpp>

In this example, note that both the Preamble and Epilogue are explicitly included.
This is strictly necessary for the Cript to compile and load correctly.

.. _cripts-overview-usage-compiler:

Compiler
--------

The compiler is a simple shell script that compiles the Cript file into a shared object
file. The script is responsible for setting up the environment and calling the compiler
with the correct arguments. The script must be executable and readable by the ATS process.

Two arguments are always given to the compiler script:

1. The path to the Cript file.
2. The path to the shared object file to create.

An example compiler script is provided in the ATS source directory, but an example is
also shown below:

.. code-block:: bash

   #!/usr/bin/env bash

   # This was done for a Mac, may need to be adjusted for other platforms.
   ATS_ROOT=/opt/ats
   CXX=clang++
   CXXFLAGS="-std=c++20 -I/opt/homebrew/include -undefined dynamic_lookup"
   STDFLAGS="-shared -fPIC -Wall -Werror -I${ATS_ROOT}/include -L${ATS_ROOT}/lib -lcripts"

   SOURCE=$1
   DEST=$2

   ${CXX} ${CXXFLAGS} ${STDFLAGS} -o $DEST $SOURCE

The example in the ATS source directory is more complex, as it also provides a basic
cache for the shared object files. This is useful for large setups with many Cript files.
In fact, moving the compilation details to be separate from the ATS process is a good
idea with a lot of flexibility for the user.

.. _cripts-overview-hooks:

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

.. _cripts-overview-hooks-do-remap:

do_remap()
^^^^^^^^^^

The ``do_remap()`` hook is called when a remap rule matches. This is the main hook for
modifying the request or response. In ATS plugin terms, this is the main entry point
for all remap plugins.

.. _cripts-overview-hooks-do-post-remap:

do_post_remap()
^^^^^^^^^^^^^^^

The ``do_post_remap()`` hook is called after the remap has been done. This can sometimes
be useful for modifying the request details *after* the remap has been done. More often
than not, this hook is not needed.

.. _cripts-overview-hooks-do-send-response:

do_send_response()
^^^^^^^^^^^^^^^^^^

The ``do_send_response()`` hook is called when the response is ready to be sent to the
client. In this hook you can modify or add to the response headers for example.

.. _cripts-overview-hooks-do-cache-lookup:

do_cache_lookup()
^^^^^^^^^^^^^^^^^

The ``do_cache_lookup()`` hook is called when a cache lookup is done. This can be useful
to take action on the response from the cache, once you know if the cache lookup succeeded
or not.

.. _cripts-overview-hooks-do-send-request:

do_send_request()
^^^^^^^^^^^^^^^^^

The ``do_send_request()`` hook is called when the request is ready to be sent to the
origin server. This is the main hook for modifying the request to the origin server.

.. _cripts-overview-hooks-do-read-response:

do_read_response()
^^^^^^^^^^^^^^^^^^

The ``do_read_response()`` hook is called when the response is being read from the origin
server. This is the main hook for modifying the response from the origin server.

.. _cripts-overview-hooks-do-txn-close:

do_txn_close()
^^^^^^^^^^^^^^

The ``do_txn_close()`` hook is called when the transaction is closed.

Instance Hooks
--------------

In addition to these normal hooks, there are also three hooks that are used for setting
up a cript and remap plugin instance. This is primarily useful when writing traditional
remap plugins in Cripts.

**Note:** These hooks are not needed for most Cripts that are used in remap rules.

.. _cripts-overview-hooks-do-init:

do_init()
^^^^^^^^^

The ``do_init()`` hook is called once when the plugin is loaded.

.. _cripts-overview-hooks-do-create-instance:

do_create_instance()
^^^^^^^^^^^^^^^^^^^^

The ``do_create_instance()`` hook is called when a new instance of the plugin is created.
Of particular interest here is the ability to read and save the instance parameters that
can be provided in the remap rule (e.g. ``@plugin=example.cript @pparam=value``).

.. _cripts-overview-hooks-do-delete-instance:

do_delete_instance()
^^^^^^^^^^^^^^^^^^^^

The ``do_delete_instance()`` hook is called when the instance is deleted.

.. _cripts-overview-instance-data:

Instance Data
=============

Instance data is a way to store data that is specific to a plugin instance. This is
primarily useful when writing traditional remap plugins in Cripts using the instance hooks.
Instance data is stored in a map that is specific to the plugin instance, and each slot
is initially a string, as provided by the remap rule.

Best way to understand this is to look at an example:

.. code-block:: cpp

   #include <cripts/Preamble.hpp>

   do_create_instance()
   {
     if (instance.size() == 3) {
       auto first  = float(AsString(instance.data[0]));
       auto second = integer(AsString(instance.data[1]));
       auto third  = boolean(AsString(instance.data[3]));

       instance.data[0] = first;
       instance.data[1] = second;
       instance.data[2] = third;
     }
   }

   do_remap()
   {
       auto first  = AsFloat(instance.data[0]);
       auto second = AsInteger(instance.data[1]);
       auto third  = AsBoolean(instance.data[3]);

       // Use the instance parameter here in the Cript rules.
   }

   #include <cripts/Epilogue.hpp>

As you can see, it takes a little bit of work to manage these instance parameters, which
is a limitation of the language constructs in Cripts. But writing plugins like this is for
more advanced users, and users just writing simple Cripts for their remap rules will not
need to worry about this.
