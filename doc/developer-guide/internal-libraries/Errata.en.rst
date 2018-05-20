.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements. See the NOTICE file distributed with this work for
   additional information regarding copyright ownership. The ASF licenses this file to you under the
   Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with
   the License. You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.

.. include:: ../../common.defs

.. default-domain:: cpp

Errata
*******

Synopsis
========

:code:`#include <tsconfig/Errata.h>`

:class:`Errata` is an error reporting class. It is designed to make it easier to report errors
to callers. It provides a stack of error messages, called *annotations*, each with a severity.
:class:`Errata` is optimized for the success (no annotations) case, which is the common case. It uses
an internal shared pointer architecture so returning instances from functions is fast (no copies).
The annotations, or "notes", are allocated in a :class:`MemArena` to minimize memory allocation costs.

Description
===========

An instance of :class:`Errata` is intended to be used as the return value of a function that can
fail non-catastrophically. It is particularly useful in cases where there may be multiple issues
discovered that need to be reported, or the function itself needs to return annotations.
:class:`Errata` makes it easy to add additional messages as the instance passes back through the
call stack.

Each annotation has a :type:`Severity` and text. The severity follows the normal ``syslog`` convention.
The :class:`Errata` also has a severity which is the maximum of the severities of all annotations.
Annotations are added to the :class:`Errata` using the method :func:`Errata::note`.

The :class:`Rv` template class is intended to be used as a function return value, to pair an existing
return value with an :class:`Errata`. This helps incremental implementation of using :class:`Errata`. The
usage pattern would be, given an existing function::

   int f();

it could be changed to

   Rv<int> f();

The :code:`Rv<int` will act in most contexts as if it were an :code:`int` with the result that usually
the caller of :code:`f` will not need code changes.

To make the transition easier, :class:`Errata` supports the concept of "sinks". When an
:class:`Errata` goes out of scope, if it contains messages it is sent to any registered sinks. These
in turn can log the data in the :class:`Errata`. If, in the example above, the caller ignores the
returned :class:`Errata` it will still be logged.

Reference
=========

.. enum-class:: Severity

   The level of severity of an annotation.

   .. enumerator:: DIAG

      Diagnostic information.

   .. enumerator:: DBG

      Debugging information [#]_.

   .. enumerator:: INFO

      Important informative information [#]_.

   .. enumerator:: WARN

      A problem occurred but has been compensated for.

   .. enumerator:: ERROR

      An error occurred, some functionality is disabled.

   .. enumerator:: FATAL

      An error occurred from which the process cannot recover.

   .. enumerator:: ALERT

      An error occurred that requires attention.

   .. enumerator:: EMERGENCY

      An error occured from which process cannot recover even if restarted.

.. class:: Errata

   A stack of messages used for error reporting.

   .. function: template < typename ... Args > Errata & note (std::string_view format, Args && ... args)

      Add an annotation to the instance. The :arg:`format` and :arg:`args` are handled by :func:`BufferWriter::print`
      and placed in to local memory of the instance.

      This method can also accept a single :class:`Errata` argument, in which case all of the annotations of
      that instance are copied to this instance.

   .. function:: Errata& clear()

      Remove all annotations.

.. class:: template < typename R > Rv

   A return value wrapper class.

Appendix
========

.. rubric:: Footnotes

.. [#]

   This is normally "DEBUG" but that is a :code:`#define` name and therefore can't be used.

.. [#]

   This is normally "STATUS" but that is a :code:`#define` name and therefore can't be used.
