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

.. _cripts-misc:

Miscellaneous
*************

Of course, there's a lot more to Cripts than :ref:`URLs <cripts-urls>`,
:ref:`headers <cripts-headers>` and :ref:`connections <cripts-connections>`. This
chapter in the Cripts documentation covers a variety of topics that don't fit
into the their own chapter.

.. _cripts-misc-errors:

Errors
======

Often it's useful to be able to abort a client transaction prematurely, and
return an error to the client. Cripts provid
making this easy.

.. note::
    Explicitly forcing an HTTP error overrides any other response status that may have been set.

=========================   =======================================================================
Function                    Description
=========================   =======================================================================
``Error::Status::Set()``    Sets the response to the status code, and force the request to error.
``Error::Status::Get()``    Get the current response status for the request.
``Error::Reason::Set()``    Sets an explicit reason message with the status code. **TBD**
=========================   =======================================================================

Example:

.. code-block:: cpp

   do_remap()
   {
     borrow req  = Client::Request::get();

     if (req["X-Header"] == "yes") {
       Error::Status::Set(403);
     }
     // Do more stuff here

     if (Error::status::Get() != 403) {
       // Do even more stuff here if we're not in error state
     }
   }

.. _cripts-misc-transaction:

ATS transactions are generally hidden within Cripts, but for power users, the
``transaction`` object provides access to the underlying transaction. In this object,
the following functions are available:

=========================   =======================================================================
Function                    Description
=========================   =======================================================================
``DisableCallback()``       Disables a future callback in this Cript, for this transaction.
``Aborted()``               Has the transaction been aborted.
``LookupStatus()``          Returns the cache lookup status for the transaction.
=========================   =======================================================================

When disabling a callback, use the following names:

=======================================   =========================================================
Callback                                  Description
=======================================   =========================================================
``Cript::Callback::DO_REMAP``             The ``do_remap()`` hook.
``Cript::Callback::DO_POST_REMAP``        The ``do_post_remap()`` hook.
``Cript::Callback::DO_SEND_RESPONSE``     The ``do_send_response()`` hook.
``Cript::Callback::DO_CACHE_LOOKUP``      The ``do_cache_lookup()`` hook.
``Cript::Callback::DO_SEND_REQUEST``      The ``do_send_request()`` hook.
``Cript::Callback::DO_READ_RESPONSE``     The ``do_read_response()`` hook.
``Cript::Callback::DO_TXN_CLOSE``         The ``do_txn_close()`` hook.
=======================================   =========================================================

Finally, the ``transaction`` object also provides access to these ATS objects, which can be used
with the lower level ATS APIs:

=========================   =======================================================================
Object                      Description
=========================   =======================================================================
``txnp``                    The TSHttpTxn pointer.
``ssnp``                    TSHttpSsn pointer.
=========================   =======================================================================

The ``transaction`` object is a global available everywhere, just like the ``instance`` object.
Example usage to turn off a particular hook conditionally:

.. code-block:: cpp

   do_remap()
   {
     static borrow req = Client::Request::get();

     if (req["X-Header"] == "yes") {
       transaction.DisableCallback(Cript::Callback::DO_READ_RESPONSE);
     }
   }

.. note::
    Disabling callbacks like this is an optimization, avoiding for the hook to be called at all.
    It can be particularly useful when the decision to run the hook is made early in the Cript.

Time
====

Cripts has encapsulated some common time-related functions in the core.  At the
moment only the localtime is available, via the ``Time::Local`` object and its
``Now()`` method. The ``Now()`` method returns the current time as an object
with the following functions:

=====================   ===========================================================================
Function                Description
=====================   ===========================================================================
``Epoch()``             Returns the number of seconds since the Unix epoch (00:00:00 UTC, January 1, 1970).
``Year()``              Returns the year.
``Month()``             Returns the month (1-12).
``Day()``               Returns the day of the month (1-31).
``Hour()``              Returns the hour (0-23).
``Minute()``            Returns the minute (0-59).
``Second()``            Returns the second (0-59).
``Weekday()``           Returns the day of the week (0-6, Sunday is 0).
``Yearday()``           Returns the day of the year (0-365).
=====================   ===========================================================================

The time as returned by ``Now()`` can also be used directly in comparisons with previous or future
times.

.. _cripts-misc-plugins:

Plugins
=======

While Cripts provides a lot of functionality out of the box, there are times
when you want to continue using existing remap plugins conditionally. Cripts
allows you to load and run existing remap plugins from within your Cripts.
This opens up new possibilities for your existing plugins, as you gain the
full power of Cript to decide when to run such plugins.

Setting up existing remap plugins must be done in the ``do_create_instance()``
hook. The instantiated remap plugins must be added to the instance object for the
Cript, using the ``addPlugin()`` method. Here's an example to run the rate limiting
plugin based on the client request headers:

.. code-block:: cpp

   do_create_instance()
   {
     instance.AddPlugin("my_ratelimit", "rate_limit.so", {"--limit=300", "--error=429"});
   }

   do_remap()
   {
     static borrow plugin = instance.plugins["my_ratelimit"];
     borrow        req    = Client::Request::Get();

     if (req["X-Header"] == "yes") {
       plugin.RunRemap();
     }
   }

.. note::
   The name of the plugin instance, as specified to ``AddPlugin()``, must be
   unique across all Cripts.

.. _cripts-misc-files:

Files
=====

In same cases, albeit not likely, you may need to read lines of text from A
file. Cripts of course allows this to be done with C or C++ standard file APIs,
but we also provide a few convenience functions to make this easier.

The ``File`` object encapsulates the common C++ files operations. For convenience,
and being such a common use case, reading a single line from a file is provided
by the ``File::Line::Reader`` object. Some examples:

.. code-block:: cpp

   do_remap()
   {
     static const File::Path p1("/tmp/foo");
     static const File::Path p2("/tmp/secret.txt");
     if (File::Status(p1).Type() == File::Type::regular) {
       resp["X-Foo-Exists"] = "yes";
     } else {
       resp["X-Foo-Exists"] = "no";
     }
     string secret = File::Line::Reader(p2);
     CDebug("Read secret = {}", secret);
   }

.. _cripts-misc-uuid:

UUID
====

Cripts supports generating a few different UUID (Universally Unique Identifier), for
different purposes. The ``UUID`` class provides the following objects:

=========================   =======================================================================
Object                      Description
=========================   =======================================================================
``UUID::Process``           Returns a UUID for the running process (changes on ATS startup).
``UUID::Unique``            Returns a completely unique UUID for the server and transacion.
``UUID::Request``           Returns a unique id for this request.
=========================   =======================================================================

Using the ``UUID`` object is simple, via the ``Get()`` method. Here's an example:

.. code-block:: cpp

   do_remap()
   {
     static borrow req = Client::Request::Get();

     resp["X-UUID"] = UUID::Unique::Get();
   }

.. _cripts-metrics:

Metrics
=======

Cripts metrics are built directly on top of the atomic core ATS metrics. As such, they not only
work the same as the core metrics, but they are also as efficient. There are two types of metrics:

=========================   =======================================================================
Metric                      Description
=========================   =======================================================================
``Metric::Counter``         A simple counter, which can only be incremented.
``Metric::Gauge``           A gauge, which can be incremented and decremented, and set to a value.
=========================   =======================================================================

Example:

.. code-block:: cpp

   do_create_instance()
   {
     instance.metrics[0] = Metrics::Counter::Create("cript.example1.instance_calls");
   }

   do_remap()
   {
     static auto plugin_metric = Metrics::Counter("cript.example1.plugin_calls");

     plugin_metric.Increment();
     instance.metrics[0]->Increment();
   }

A ``Metric::Gauge`` can also be set via the ``Setter()`` method.
