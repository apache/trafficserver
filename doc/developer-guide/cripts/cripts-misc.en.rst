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
return an error to the client. Cripts provides a few convenience functions for
making this easy.

.. note::
    Explicitly forcing an HTTP error overrides any other response status that may have been set.

=========================   =======================================================================
Function                    Description
=========================   =======================================================================
``Error::Status::set``      Sets the response to the status code, and force the request to error.
``Error::Reason::set``      Sets an explicit reason message with the status code. **TBD**
=========================   =======================================================================

Example:

.. code-block:: cpp

   do_remap()
   {
     borrow req  = Client::Request::get();

     if (req["X-Header"] == "yes") {
       Error::Status::set(403);
     }
   }

.. _cripts-misc-transaction:

ATS transactions are generally hidden within Cripts, but for power users, the
``transaction`` object provides access to the underlying transaction. In this object,
the following functions are available:

=========================   =======================================================================
Function                    Description
=========================   =======================================================================
``disableCallback()``       Disables a future callback in this Cript, for this transaction.
``aborted()``               Has the transaction been aborted.
``lookupStatus()``          Returns the cache lookup status for the transaction.
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
       transaction.disableCallback(Cript::Callback::DO_READ_RESPONSE);
     }
   }

.. note::
    Disabling callbacks like this is an optimization, avoding for the hook to be called at all.
    It can be particularly useful when the decision to run the hook is made early in the Cript.

Time
====

Cripts has encapsulated some common time-related functions in the core.  At the
moment only the localtime is available, via the ``Time::Local`` object and its
``now()`` method. The ``now()`` method returns the current time as an object
with the following functions:

=====================   ===========================================================================
Function                Description
=====================   ===========================================================================
``epoch()``             Returns the number of seconds since the Unix epoch (00:00:00 UTC, January 1, 1970).
``year()``              Returns the year.
``month()``             Returns the month (1-12).
``day()``               Returns the day of the month (1-31).
``hour()``              Returns the hour (0-23).
``minute()``            Returns the minute (0-59).
``second()``            Returns the second (0-59).
``weekday()``           Returns the day of the week (0-6, Sunday is 0).
``yearday()``           Returns the day of the year (0-365).
=====================   ===========================================================================

The time as returned by ``now()`` can also be used directly in comparisons with previous or future
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
hook. The instanatiated remap plugins must be added to the instance object for the
Cript, using the ``addPlugin()`` method. Here's an example to run the rate limiting
plugin based on the client request headers:

.. code-block:: cpp

   do_create_instance()
   {
     instance.addPlugin("my_ratelimit", "rate_limit.so", {"--limit=300", "--error=429"});
   }

   do_remap()
   {
     static borrow plugin = instance.plugins["my_ratelimit"];
     borrow        req    = Client::Request::get();

     if (req["X-Header"] == "yes") {
       plugin.runRemap();
     }
   }

.. note::
   The name of the plugin instance, as specified to ``addPlugin()``, must be
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
     if (File::Status(p1).type() == File::Type::regular) {
       resp["X-Foo-Exists"] = "yes";
     } else {
       resp["X-Foo-Exists"] = "no";
     }
     string secret = File::Line::Reader(p2);
     CDebug("Read secret = {}", secret);
   }
