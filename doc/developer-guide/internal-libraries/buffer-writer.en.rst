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

.. _BufferWriter:

BufferWriter
*************

Synopsis
++++++++

.. code-block:: cpp

   #include <ts/BufferWriterForward.h> // Custom formatter support only.
   #include <ts/BufferWriter.h> // General usage.

Description
+++++++++++

:class:`BufferWriter` is intended to increase code reliability and reduce complexity in the common
circumstance of generating formatted output strings in fixed buffers. Current usage is a mixture of
:code:`snprintf` and :code:`memcpy` which provides a large scope for errors and verbose code to
check for buffer overruns. The goal is to provide a wrapper over buffer size tracking to make such
code simpler and less vulnerable to implementation error.

:class:`BufferWriter` itself is an abstract class to describe the base interface to wrappers for
various types of output buffers. As a common example, :class:`FixedBufferWriter` is a subclass
designed to wrap a fixed size buffer. :class:`FixedBufferWriter` is constructed by passing it a
buffer and a size, which it then tracks as data is written. Writing past the end of the buffer is
clipped to prevent overruns.

Consider current code that looks like this.

.. code-block:: cpp

   char buff[1024];
   char * ptr = buff;
   size_t len = sizeof(buff);
   //...
   if (len > 0) {
     auto n = std::min(len, thing1_len);
     memcpy(ptr, thing1, n);
     len -= n;
   }
   if (len > 0) {
     auto n = std::min(len, thing2_len);
     memcpy(ptr, thing2, n);
     len -= n;
   }
   if (len > 0) {
     auto n = std::min(len, thing3_len);
     memcpy(ptr, thing3, n);
     len -= n;
   }

This is changed to

.. code-block:: cpp

   char buff[1024];
   ts::FixedBufferWriter bw(buff, sizeof(buff));
   //...
   bw.write(thing1, thing1_len);
   bw.write(thing2, thing2_len);
   bw.write(thing3, thing3_len);

The remaining length is updated every time and checked every time. A series of checks, calls to
:code:`memcpy`, and size updates become a simple series of calls to :func:`BufferWriter::write`.

For other types of interaction, :class:`FixedBufferWriter` provides access to the unused buffer via
:func:`BufferWriter::auxBuffer` and :func:`BufferWriter::remaining`. This makes it possible to easily
use :code:`snprintf`, given that :code:`snprint` returns the number of bytes written.
:func:`BufferWriter::fill` is used to indicate how much of the unused buffer was used. Therefore
something like (riffing off the previous example)::

   if (len > 0) {
      len -= snprintf(ptr, len, "format string", args...);
   }

becomes::

   bw.fill(snprintf(bw.auxBuffer(), bw.remaining(),
           "format string", args...));

By hiding the length tracking and checking, the result is a simple linear sequence of output chunks,
making the logic much easier to follow.

Usage
+++++

The header files are divided in to two variants. :ts:git:`include/tscore/BufferWriter.h` provides the basic
capabilities of buffer output control. :ts:git:`include/tscore/BufferWriterForward.h` provides the basic
:ref:`formatted output mechanisms <bw-formatting>`, primarily the implementation and ancillary
classes for :class:`BWFSpec` which is used to build formatters.

:class:`BufferWriter` is an abstract base class, in the style of :code:`std::ostream`. There are
several subclasses for various use cases. When passing around this is the common type.

:class:`FixedBufferWriter` writes to an externally provided buffer of a fixed length. The buffer must
be provided to the constructor. This will generally be used in a function where the target buffer is
external to the function or already exists.

:class:`LocalBufferWriter` is a templated class whose template argument is the size of an internal
buffer. This is useful when the buffer is local to a function and the results will be transferred
from the buffer to other storage after the output is assembled. Rather than having code like::

   char buff[1024];
   ts::FixedBufferWriter bw(buff, sizeof(buff));

it can be written more compactly as::

   ts::LocalBufferWriter<1024> bw;

In many cases, when using :class:`LocalBufferWriter` this is the only place the size of the buffer
needs to be specified and therefore can simply be a constant without the overhead of defining a size
to maintain consistency. The choice between :class:`LocalBufferWriter` and :class:`FixedBufferWriter`
comes down to the owner of the buffer - the former has its own buffer while the latter operates on
a buffer owned by some other object. Therefore if the buffer is declared locally, use
:class:`LocalBufferWriter` and if the buffer is received from an external source (such as via a
function parameter) use :class:`FixedBufferWriter`.

Writing
-------

The basic mechanism for writing to a :class:`BufferWriter` is :func:`BufferWriter::write`.
This is an overloaded method for a character (:code:`char`), a buffer (:code:`void *, size_t`)
and a string view (:code:`std::string_view`). Because there is a constructor for :code:`std::string_view`
that takes a :code:`const char*` as a C string, passing a literal string works as expected.

There are also stream operators in the style of C++ stream I/O. The basic template is

.. code-block:: cpp

   template < typename T > ts::BufferWriter& operator << (ts::BufferWriter& w, T const& t);

The stream operators are provided as a convenience, the primary mechanism for formatted output is
via overloading the :func:`bwformat` function. Except for a limited set of cases the stream operators
are implemented by calling :func:`bwformat` with the Buffer Writer, the argument, and a default
format specification.

Reading
-------

Data in the buffer can be extracted using :func:`BufferWriter::data`. This and
:func:`BufferWriter::size` return a pointer to the start of the buffer and the amount of data
written to the buffer. This is effectively the same as :func:`BufferWriter::view` which returns a
:code:`std::string_view` which covers the output data. Calling :func:`BufferWriter::error` will indicate
if more data than space available was written (i.e. the buffer would have been overrun).
:func:`BufferWriter::extent` returns the amount of data written to the :class:`BufferWriter`. This
can be used in a two pass style with a null / size 0 buffer to determine the buffer size required
for the full output.

Advanced
--------

The :func:`BufferWriter::clip` and :func:`BufferWriter::extend` methods can be used to reserve space
in the buffer. A common use case for this is to guarantee matching delimiters in output if buffer
space is exhausted. :func:`BufferWriter::clip` can be used to temporarily reduce the buffer size by
an amount large enough to hold the terminal delimiter. After writing the contained output,
:func:`BufferWriter::extend` can be used to restore the capacity and then output the terminal
delimiter.

.. warning:: **Never** call :func:`BufferWriter::extend` without previously calling :func:`BufferWriter::clip` and always pass the same argument value.

:func:`BufferWriter::remaining` returns the amount of buffer space not yet consumed.

:func:`BufferWriter::auxBuffer` returns a pointer to the first byte of the buffer not yet used. This
is useful to do speculative output, or do bounded output in a manner similar to using
:func:`BufferWriter::clip` and :func:`BufferWriter::extend`. A new :class:`BufferWriter` instance
can be constructed with

.. code-block:: cpp

   ts::FixedBufferWriter subw(w.auxBuffer(), w.remaining());

or as a convenience ::

   ts::FixedBuffer subw{w.auxBuffer()};

Output can be written to :arg:`subw`. If successful, then :code:`w.fill(subw.size())` will add that
output to the main buffer. Depending on the purpose, :code:`w.fill(subw.extent())` can be used -
this will track the attempted output if sizing is important. Note that space for any terminal
markers can be reserved by bumping down the size from :func:`BufferWriter::remaining`. Be careful of
underrun as the argument is an unsigned type.

If there is an error then :arg:`subw` can be ignored and some suitable error output written to
:arg:`w` instead. A common use case is to verify there is sufficient space in the buffer and create
a "not enough space" message if not. E.g. ::

   ts::FixedBufferWriter subw{w.auxWriter()};
   this->write_some_output(subw);
   if (!subw.error()) w.fill(subw.size());
   else w.write("Insufficient space"sv);

Examples
++++++++

For example, error prone code that looks like

.. code-block:: cpp

   char new_via_string[1024]; // 512-bytes for hostname+via string, 512-bytes for the debug info
   char * via_string = new_via_string;
   char * via_limit  = via_string + sizeof(new_via_string);

   // ...

   * via_string++ = ' ';
   * via_string++ = '[';

   // incoming_via can be max MAX_VIA_INDICES+1 long (i.e. around 25 or so)
   if (s->txn_conf->insert_request_via_string > 2) { // Highest verbosity
      via_string += nstrcpy(via_string, incoming_via);
   } else {
      memcpy(via_string, incoming_via + VIA_CLIENT, VIA_SERVER - VIA_CLIENT);
      via_string += VIA_SERVER - VIA_CLIENT;
   }
   *via_string++ = ']';

becomes

.. code-block:: cpp

   ts::LocalBufferWriter<1024> w; // 1K internal buffer.

   // ...

   w.write(" ["sv);
   if (s->txn_conf->insert_request_via_string > 2) { // Highest verbosity
      w.write(incoming_via);
   } else {
      w.write(std::string_view{incoming_via + VIA_CLIENT, VIA_SERVER - VIA_CLIENT});
   }
   w.write(']');

There will be no overrun on the memory buffer in :arg:`w`, in strong contrast to the original code.
This can be done better, as ::

   if (w.remaining() >= 3) {
      w.clip(1).write(" ["sv);
      if (s->txn_conf->insert_request_via_string > 2) { // Highest verbosity
         w.write(incoming_via);
      } else {
         w.write(std::string_view{incoming_via + VIA_CLIENT, VIA_SERVER - VIA_CLIENT});
      }
      w.extend(1).write(']');
   }

This has the result that the terminal bracket will always be present which is very much appreciated
by code that parses the resulting log file.

.. _bw-formatting:

Formatted Output
++++++++++++++++

The base :class:`BufferWriter` was made to provide memory safety for formatted output. Support for
formatted output was made to provide *type* safety. The implementation deduces the types of the
arguments to be formatted and handles them in a type specific and safe way.

The formatting style is of the "prefix" or "printf" style - the format is specified first and then
all the arguments. This contrasts to the "infix" or "streaming" style where formatting, literals,
and argument are intermixed in the order of output. There are various arguments for both styles but
conversations within the |TS| community indicated a clear preference for the prefix style. Therefore
formatted out consists of a format string, containing *formats*, which are replaced during output
with the values of arguments to the print function.

The primary use case for formatting is formatted output to fixed buffers. This is by far the
dominant style of output in |TS| and during the design phase I was told any performance loss must be
minimal. While work has and will be done to extend :class:`BufferWriter` to operate on non-fixed
buffers, such use is secondary to operating directly on memory.

.. important::

   The overriding design goal is to provide the type specific formatting and flexibility of C++
   stream operators with the performance of :code:`snprintf` and :code:`memcpy`.

This will preserve the general style of output in |TS| while still reaping the benefits of type safe
formatting with little to no performance cost.

Type safe formatting has two major benefits -

*  No mismatch between the format specifier and the argument. Although some modern compilers do
   better at catching this at run time, there is still risk (especially with non-constant format
   strings) and divergence between operating systems such that there is no `universally correct
   choice <https://github.com/apache/trafficserver/pull/3476/files>`__. In addition the number of
   arguments can be verified to be correct which is often useful.

*  Formatting can be customized per type or even per partial type (e.g. :code:`T*` for generic
   :code:`T`). This enables embedding common formatting work in the format system once, rather than
   duplicating it in many places (e.g. converting enum values to names). This makes it easier for
   developers to make useful error messages. See :ref:`this example <bwf-http-debug-name-example>`
   for more detail.

As a result of these benefits there has been other work on similar projects, to replace
:code:`printf` a better mechanism. Unfortunately most of these are rather project specific and don't
suit the use case in |TS|. The two best options, `Boost.Format
<https://www.boost.org/doc/libs/1_64_0/libs/format/>`__ and `fmt <https://github.com/fmtlib/fmt>`__,
while good, are also not quite close enough to outweigh the benefits of a version specifically
tuned for |TS|. ``Boost.Format`` is not acceptable because of the Boost footprint. ``fmt`` has the
problem of depending on C++ stream operators and therefore not having the required level of
performance or memory characteristics. Its main benefit, of reusing stream operators, doesn't apply
to |TS| because of the nigh non-existence of such operators. The possibility of using C++ stream
operators was investigated but changing those to use pre-existing buffers not allocated internally
was very difficult, judged worse than building a relatively simple implementation from scratch. The
actual core implementation of formatted output for :class:`BufferWriter` is not very large - most of
the overall work will be writing formatters, work which would need to be done in any case but in
contrast to current practice, only done once.

:class:`BufferWriter` supports formatting output in a style similar to Python formatting via
:func:`BufferWriter::print`. Looking at the other versions of work in this area, almost all of them
have gone with this style. Boost.Format also takes basically this same approach, just using
different paired delimiters. |TS| contains increasing amounts of native Python code which means many
|TS| developers will already be familiar (or should become familiar) with this style of formatting.
While not *exactly* the same at the Python version, BWF (:class:`BufferWriter` Formatting) tries to
be as similar as language and internal needs allow.

As noted previously and in the Python and even :code:`printf` way, a format string consists of
literal text in which formats are embedded. Each format marks a place where formatted data of
an argument will be placed, along with argument specific formatting. The format is divided in to
three parts, separated by colons.

While this seems a bit complex, all of it is optional. If default output is acceptable, then BWF
will work with just the format ``{}``. In a sense, ``{}`` serves the same function for output as
:code:`auto` does for programming - the compiler knows the type, it should be able to do something
reasonable without the programmer needing to be explicit.

.. productionlist:: format
   format: "{" [name] [":" [specifier] [":" extension]] "}"
   name: index | ICHAR+
   index: non-negative integer
   specifier: <see below>
   extension: ICHAR*
   ICHAR: a printable ASCII character except for '{', '}', ':'

:token:`~format:name`
   The name of the argument to use. This can be a non-negative integer in which case it is
   the zero based index of the argument to the method call. E.g. ``{0}`` means the first argument
   and ``{2}`` is the third argument after the format.

      ``bw.print("{0} {1}", 'a', 'b')`` => ``a b``

      ``bw.print("{1} {0}", 'a', 'b')`` => ``b a``

   The name can be omitted in which case it is treated as an index in parallel to the
   position in the format string. Only the position in the format string matters, not what names
   other format elements may have used.

      ``bw.print("{0} {2} {}", 'a', 'b', 'c')`` => ``a c c``

      ``bw.print("{0} {2} {2}", 'a', 'b', 'c')`` => ``a c c``

   Note that an argument can be printed more than once if the name is used more than once.

      ``bw.print("{0} {} {0}", 'a', 'b')`` => ``a b a``

      ``bw.print("{0} {1} {0}", 'a', 'b')`` => ``a b a``

   Alphanumeric names refer to values in a global table. These will be described in more detail
   someday. Such names, however, do not count in terms of default argument indexing.

:token:`~format:specifier`
   Basic formatting control.

   .. productionlist:: spec
      specifier: [[fill]align][sign]["#"]["0"][[min][.precision][,max][type]]
      fill: fill-char | URI-char
      URI-char: "%" hex-digit hex-digit
      fill-char: printable character except "{", "}", ":", "%"
      align: "<" | ">" | "=" | "^"
      sign: "+" | "-" | " "
      min: non-negative integer
      precision: positive integer
      max: non-negative integer
      type: type: "g" | "s" | "S" | "x" | "X" | "d" | "o" | "b" | "B" | "p" | "P"
      hex-digit: "0" .. "9" | "a" .. "f" | "A" .. "F"

   The output is placed in a field that is at least :token:`~spec:min` wide and no more than
   :token:`~spec:max` wide. If the output is less than :token:`~spec:min` then

      *  The :token:`~spec:fill` character is used for the extra space required. This can be an
         explicit character or a URI encoded one (to allow otherwise reserved characters).

      *  The output is shifted according to the :token:`~spec:align`.

         <
            Align to the left, fill to the right.

         >
            Align to the right, fill to the left.

         ^
            Align in the middle, fill to left and right.

         =
            Numerically align, putting the fill between the sign character and the value.

   The output is clipped by :token:`~spec:max` width characters and by the end of the buffer.
   :token:`~spec:precision` is used by floating point values to specify the number of places of
   precision.

   :token:`~spec:type` is used to indicate type specific formatting. For integers it indicates the
   output radix and if ``#`` is present the radix is prefix is generated (one of ``0xb``, ``0``,
   ``0x``). Format types of the same letter are equivalent, varying only in the character case used
   for output. Most commonly 'x' prints values in lower cased hexadecimal (:code:`0x1337beef`) while
   'X' prints in upper case hexadecimal (:code:`0X1337BEEF`). Note there is no upper case decimal or
   octal type because case is irrelevant for those.

      = ===============
      g generic, default.
      b binary
      B Binary
      d decimal
      o octal
      x hexadecimal
      X Hexadecimal
      p pointer (hexadecimal address)
      P Pointer (Hexadecimal address)
      s string
      S String (upper case)
      = ===============

   For several specializations the hexadecimal format is taken to indicate printing the value as if
   it were a hexadecimal value, in effect providing a hex dump of the value. This is the case for
   :code:`std::string_view` and therefore a hex dump of an object can be done by creating a
   :code:`std::string_view` covering the data and then printing it with :code:`{:x}`.

   The string type ('s' or 'S') is generally used to cause alphanumeric output for a value that would
   normally use numeric output. For instance, a :code:`bool` is normally ``0`` or ``1``. Using the
   type 's' yields ``true` or ``false``. The upper case form, 'S', applies only in these cases where the
   formatter generates the text, it does not apply to normally text based values unless specifically noted.

:token:`~format:extension`
   Text (excluding braces) that is passed to the type specific formatter function. This can be used
   to provide extensions for specific argument types (e.g., IP addresses). The base logic ignores it
   but passes it on to the formatting function which can then behave different based on the
   extension.

Usage Examples
--------------

Some examples, comparing :code:`snprintf` and :func:`BufferWriter::print`. ::

   if (len > 0) {
      auto n = snprintf(buff, len, "count %d", count);
      len -= n;
      buff += n;
   }

   bw.print("count {}", count);

   // --

   if (len > 0) {
      auto n = snprintf(buff, len, "Size %" PRId64 " bytes", sizeof(thing));
      len -= n;
      buff += n;
   }

   bw.print("Size {} bytes", sizeof(thing));

   // --

   if (len > 0) {
      auto n = snprintf(buff, len, "Number of items %ld", thing->count());
      len -= n;
      buff += n;
   }

   bw.print("Number of items {}", thing->count());

Enumerations become easier. Note in this case argument indices are used in order to print both a
name and a value for the enumeration. A key benefit here is the lack of need for a developer to know
the specific free function or method needed to do the name lookup. In this case,
:code:`HttpDebugNames::get_server_state_name`. Rather than every developer having to memorize the
association between the type and the name lookup function, or grub through the code hoping for an
example, the compiler is told once and henceforth does the lookup. The internal implementation of
this is :ref:`here <bwf-http-debug-name-example>` ::

   if (len > 0) {
      auto n = snprintf(buff, len, "Unexpected event %d in state %s[%d] for %.*s",
         event,
         HttpDebugNames::get_server_state_name(t_state.current.state),
         t_state.current.state,
         static_cast<int>(host_len), host);
      buff += n;
      len -= n;
   }

   bw.print("Unexpected event {0} in state {1}[{1:d}] for {2}",
      event, t_state.current.state, std::string_view{host, host_len});

Using :code:`std::string`, which illustrates the advantage of a formatter overloading knowing how to
get the size from the object and not having to deal with restrictions on the numeric type (e.g.,
that :code:`%.*s` requires an :code:`int`, not a :code:`size_t`). ::

   if (len > 0) {
      len -= snprintf(buff, len, "%.*s", static_cast<int>(s.size()), s.data);
   }

   bw.print("{}", s);

IP addresses are much easier. There are two big advantages here. One is not having to know the
conversion function name. The other is the lack of having to declare local variables and having to
remember what the appropriate size is. Beyond there this code is more performant because the output
is rendered directly in the output buffer, not rendered to a temporary and then copied over. This
lack of local variables can be particularly nice in the context of a :code:`switch` statement where
local variables for a :code:`case` mean having to add extra braces, or declare the temporaries at an
outer scope. ::

   char ip_buff1[INET6_ADDRPORTSTRLEN];
   char ip_buff2[INET6_ADDRPORTSTRLEN];
   ats_ip_nptop(ip_buff1, sizeof(ip_buff1), addr1);
   ats_ip_nptop(ip_buff2, sizeof(ip_buff2), add2);
   if (len > 0) {
      snprintf(buff, len, "Connecting to %s from %s", ip_buff1, ip_buff2);
   }

   bw.print("Connecting to {} from {}", addr1, addr2);

User Defined Formatting
+++++++++++++++++++++++

To get the full benefit of type safe formatting it is necessary to provide type specific formatting
functions which are called when a value of that type is formatted. This is how type specific
knowledge such as the names of enumeration values are encoded in a single location. Additional type
specific formatting can be provided via the :token:`~format:extension` field. Without this, special
formatting requires extra functions and additional work at the call site, rather than a single
consolidated formatting function.

To provide a formatter for a type :code:`V` the function :code:`bwformat` is overloaded. The signature
would look like this::

   BufferWriter& ts::bwformat(BufferWriter& w, BWFSpec const& spec, V const& v)

:arg:`w` is the output and :arg:`spec` the parsed specifier, including the extension (if any). The
calling framework will handle basic alignment as per :arg:`spec` therefore the overload does not need
to unless the alignment requirements are more detailed (e.g. integer alignment operations) or
performance is critical. In the latter case the formatter should make sure to use at least the
minimum width in order to disable any additional alignment operation.

It is important to note that a formatter can call another formatter. For example, the formatter for
pointers looks like::

   // Pointers that are not specialized.
   inline BufferWriter &
   bwformat(BufferWriter &w, BWFSpec const &spec, const void * ptr)
   {
      BWFSpec ptr_spec{spec};
      ptr_spec._radix_lead_p = true;
      if (ptr_spec._type == BWFSpec::DEFAULT_TYPE || ptr_spec._type == 'p') {
         // if default or specifically 'p', switch to lower case hex.
         ptr_spec._type = 'x';
      } else if (ptr_spec._type == 'P') {
         // Incoming 'P' means upper case hex.
         ptr_spec._type = 'X';
      }
      return bw_fmt::Format_Integer(w, ptr_spec,
         reinterpret_cast<intptr_t>(ptr), false);
   }

The code checks if the type ``p`` or ``P`` was used in order to select the appropriate case, then
delegates the actual rendering to the integer formatter with a type of ``x`` or ``X`` as
appropriate. In turn other formatters, if given the type ``p`` or ``P`` can cast the value to
:code:`const void*` and call :code:`bwformat` on that to output the value as a pointer.

To help reduce duplication, the output stream operator :code:`operator<<` is defined to call this
function with a default constructed :code:`BWFSpec` instance so that absent a specific overload
a BWF formatter will also provide a C++ stream output operator.

Enum Example
------------

.. _bwf-http-debug-name-example:

For a specific example of using BufferWriter formatting to make debug messages easier, consider the
case of :code:`HttpDebugNames`. This is a class that serves as a namespace to provide various
methods that convert state machine related data into descriptive strings. Currently this is
undocumented (and even uncommented) and is therefore used infrequently, as that requires either
blind cut and paste, or tracing through header files to understand the code. This can be greatly
simplified by adding formatters to :ts:git:`proxy/http/HttpDebugNames.h` ::

   inline ts::BufferWriter &
   bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, HttpTransact::ServerState_t state)
   {
      if (spec.has_numeric_type()) {
         // allow the user to force numeric output with '{:d}' or other numeric type.
         return bwformat(w, spec, static_cast<uintmax_t>(state));
      } else {
         return bwformat(w, spec, HttpDebugNames::get_server_state_name(state));
      }
   }

With this in place, any one wanting to print the name of the server state enumeration can do ::

   bw.print("state {}", t_state.current_state);

There is no need to remember names like :code:`HttpDebugNames` nor which method in it does the
conversion. The developer making the :code:`HttpDebugNames` class or equivalent can take care of
that in the same header file that provides the type.

.. note::

   In actual practice, due to this method being so obscure it's not actually used as far as I
   can determine.

Argument Forwarding
-------------------

It will frequently be useful for other libraries to allow local formatting (such as :code:`Errata`).
For such cases the class methods will need to take variable arguments and then forward them on to
the formatter. :class:`BufferWriter` provides the :func:`BufferWriter::printv` overload for this
purpose. Instead of taking variable arguments, these overloads take a :code:`std::tuple` of
arguments. Such as tuple is easily created with `std::forward_as_tuple
<http://en.cppreference.com/w/cpp/utility/tuple/forward_as_tuple>`__. A standard implementation that
uses the :code:`std::string` overload for :func:`bwprint` would look like ::

   template < typename ... Args >
   std::string message(string_view fmt, Args &&... args) {
      std::string zret;
      return ts::bwprint(zret, fmt, std::forward_as_tuple(args...));
   }

This gathers the argument (generally references to the arguments) in to a single tuple which is then
passed by reference, to avoid restacking the arguments for every nested function call. In essence the
arguments are put on the stack (inside the tuple) once and a reference to that stack is passed to
nested functions.

Specialized Types
-----------------

These are types for which there exists a type specific BWF formatter.

:code:`std::string_view`
   Generally the contents of the view.

   'x' or 'X'
      A hexadecimal dump of the contents of the view in lower ('x') or upper ('X') case.

   'p' or 'P'
      The pointer and length value of the view in lower ('p') or upper ('P') case.

   The :token:`~spec:precision` is interpreted specially for this type to mean "skip
   :token:`~spec:precision` initial characters". When combined with :token:`~spec:max` this allows a
   mechanism for printing substrings of the :code:`std::string_view`. For instance, to print the
   10th through 20th characters the format ``{:.10,20}`` would suffice. Given the method
   :code:`substr` for :code:`std::string_view` is cheap, it's unclear how useful this is.

:code:`sockaddr const*`
   The IP address is printed. Fill is used to fill in address segments if provided, not to the
   minimum width if specified. :class:`IpEndpoint` and :class:`IpAddr` are supported with the same
   formatting. The formatting support in this case is extensive because of the commonality and
   importance of IP address data.

   Type overrides

      'p' or 'P'
         The pointer address is printed as hexadecimal lower ('p') or upper ('P') case.

   The extension can be used to control which parts of the address are printed. These can be in any order,
   the output is always address, port, family. The default is the equivalent of "ap". In addition, the
   character '=' ("numeric align") can be used to internally right justify the elements.

   'a'
      The address.

   'p'
      The port (host order).

   'f'
      The IP address family.

   '='
      Internally justify the numeric values. This must be the first or second character. If it is the second
      the first character is treated as the internal fill character. If omitted '0' (zero) is used.

   E.g. ::

      void func(sockaddr const* addr) {
        bw.print("To {}", addr); // -> "To 172.19.3.105:4951"
        bw.print("To {0::a} on port {0::p}", addr); // -> "To 172.19.3.105 on port 4951"
        bw.print("To {::=}", addr); // -> "To 127.019.003.105:04951"
        bw.print("Using address family {::f}", addr);
        bw.print("{::a}",addr);      // -> "172.19.3.105"
        bw.print("{::=a}",addr);     // -> "172.019.003.105"
        bw.print("{::0=a}",addr);    // -> "172.019.003.105"
        bw.print("{:: =a}",addr);    // -> "172. 19.  3.105"
        bw.print("{:>20:a}",addr);   // -> "        172.19.3.105"
        bw.print("{:>20:=a}",addr);  // -> "     172.019.003.105"
        bw.print("{:>20: =a}",addr); // -> "     172. 19.  3.105"
      }

Format Classes
++++++++++++++

Although the extension for a format can be overloaded to provide additional features, this can become
too confusing and complex to use if it is used for fundamentally different semantics on the same
based type. In that case it is better to provide a format wrapper class that holds the base type
but can be overloaded to produce different (wrapper class based) output. The classic example is
:code:`errno` which is an integral type but frequently should be formatted with additional information
such as the descriptive string for the value. To do this the format wrapper class :code:`ts::bwf::Errno`
is provided. Using it is simple::

   w.print("File not open - {}", ts::bwf::Errno(errno));

which will produce output that looks like

   "File not open - EACCES: Permission denied [13]"

For :code:`errno` this is handy in another way as :code:`ts::bwf::Errno` will preserve the value of
:code:`errno` across other calls that might change it. E.g.::

   ts::bwf::Errno last_err(errno);
   // some other code generating diagnostics that might tweak errno.
   w.print("File not open - {}", last_err);

This can also be useful for user defined data types. For instance, in the HostDB the type of the entry
is printed in multiple places and each time this code is repeated ::

      "%s%s %s", r->round_robin ? "Round-Robin" : "",
         r->reverse_dns ? "Reverse DNS" : "", r->is_srv ? "SRV" : "DNS"

This could be wrapped in a class, :code:`HostDBType` such as ::

   struct HostDBType {
      HostDBInfo* _r { nullptr };
      HostDBType(r) : _r(r) {}
   };

Then define a formatter for the wrapper ::

   BufferWriter& bwformat(BufferWriter& w, BWFSpec const& spec, HostDBType const& wrap) {
     return w.print("{}{} {}", wrap._r->round_robin ? "Round-Robin" : "",
        r->reverse_dns ? "Reverse DNS" : "",
        r->is_srv ? "SRV" : "DNS");
   }

Now this can be output elsewhere with just

   w.print("{}", HostDBType(r));

If this is used multiple places, this is cleaner and more robust as it can be updated everywhere with a
change in a single code location.

These are the existing format classes in header file ``bfw_std_format.h``. All are in the :code:`ts::bwf` namespace.

.. class:: Errno

   Formatting for :code:`errno`. Generically the formatted output is the short name, the description,
   and the numeric value. A format type of ``d`` will generate just the numeric value, while a format
   type of ``s`` will generate just the short name and description.

   .. function:: Errno(int errno)

      Initialize the instance with the error value :arg:`errno`.

.. function:: template < typename ... Args > FirstOf(Args && ... args)

   Print the first non-empty string in an argument list. All arguments must be convertible to
   :code:`std::string_view`.

   By far the most common case is the two argument case used to print a special string if the base
   string is null or empty. For instance, something like this::

      w.print("{}", name != nullptr ? name : "<void>")

   This could also be done like::

      w.print("{}", ts::bwf::FirstOf(name, "<void>"));

   In addition, if the first argument is a local variable that exists only to do the empty check, that
   variable can eliminated entirely. E.g.::

      const char * name = thing.get_name();
      w.print("{}", name != nullptr ? name : "<void>")

   can be simplified to

      w.print("{}", ts::bwf::FirstOf(thing.get_name(), "<void>"));

   In general avoiding ternary operators in the print argument list makes the code cleaner and
   easier to understand.

.. class:: Date

   Date formatting in the :code:`strftime` style.

   .. function:: Date(time_t epoch, std::string_view fmt = "%Y %b %d %H:%M:%S")

      :arg:`epoch` is the time to print. :arg:`fmt` is the format for printing which is identical to
      that of `strftime <https://linux.die.net/man/3/strftime>`__. The default format looks like
      "2018 Jun 08 13:55:37".

   .. function:: Date(std::string_view fmt = "%Y %b %d %H:%M:%S")

      As previous except the epoch is the current epoch at the time the constructor is invoked.
      Therefore if the current time is to be printed the default constructor can be used.

   When used the format specification can take an extension of "local" which formats the time as
   local time. Otherwise it is GMT. ``w.print("{}", Date("%H:%M"));`` will print the hour and minute
   as GMT values. ``w.print("{::local}", Date("%H:%M"));`` will When used the format specification
   can take an extension of "local" which formats the time as local time. Otherwise it is GMT.
   ``w.print("{}", Date("%H:%M"));`` will print the hour and minute as GMT values.
   ``w.print("{::local}", Date("%H:%M"));`` will print the hour and minute in the local time zone.
   ``w.print("{::gmt}"), ...);`` will output in GMT if additional explicitness is desired.

.. class:: OptionalAffix

   Affix support for printing optional strings. This enables printing a string such the affixes are
   printed only if the string is not empty. An empty string (or :code:`nullptr`) yields no output. A
   common situation in which is this is useful is code like ::

      printf("%s%s", data ? data : "", data ? " " : "");

   or something like ::

      if (data) {
         printf("%s ", data);
      }

   Instead :class:`OptionalAffix` can be used in line, which is easier if there are multiple items. E.g.

      w.print("{}", ts::bwf::OptionalAffix(data)); // because default is single trailing space suffix.

   .. function:: OptionalAffix(const char* text, std::string_view suffix = " ", std::string_view prefix = "")

      Create a format wrapper with :arg:`suffix` and :arg:`prefix`. If :arg:`text` is
      :code:`nullptr` or is empty generate no output. Otherwise print the :arg:`prefix`,
      :arg:`text`, :arg:`suffix`.

   .. function:: OptionalAffix(std::string_view text, std::string_view suffix = " ", std::string_view prefix = "")

      Create a format wrapper with :arg:`suffix` and :arg:`prefix`. If :arg:`text` is
      :code:`nullptr` or is empty generate no output. Otherwise print the :arg:`prefix`,
      :arg:`text`, :arg:`suffix`. Note that passing :code:`std::string` as the first argument will
      work for this overload.

Global Names
++++++++++++

As a convenience, there are a few predefined global names that can be used to generate output. These
do not take any arguments to :func:`BufferWriter::print`, the data needed for output is either
process or thread global and is retrieved directly. They also are not counted for automatic indexing.

now
   The epoch time in seconds.

tick
   The high resolution clock tick.

timestamp
   UTC time in the format "Year Month Date Hour:Minute:Second", e.g. "2018 Apr 17 14:23:47".

thread-id
   The id of the current thread.

thread-name
   The name of the current thread.

ts-thread
   A pointer to the |TS| :class:`Thread` object for the current thread. This is useful for comparisons.

ts-ethread
   A pointer to the |TS| :class:`EThread` object for the current thread. This is useful for comparisons
   or to indicate if the thread is an :class:`EThread` (if not, the value will be :code:`nullptr`).

For example, to have the same output as the normal diagnostic messages with a timestamp and the current thread::

   bw.print("{timestamp} {ts-thread} Counter is {}", counter);

Note that even though no argument is provided the global names do not count as part of the argument
indexing, therefore the preceding example could be written as::

   bw.print("{timestamp} {ts-thread} Counter is {0}", counter);

Working with standard I/O
+++++++++++++++++++++++++

:class:`BufferWriter` can be used with some of the basic I/O functionality of a C++ environment. At the lowest
level the output stream operator can be used with a file descriptor or a :code:`std::ostream`. For these
examples assume :code:`bw` is an instance of :class:`BufferWriter` with data in it.

.. code-block:: cpp

   int fd = open("some_file", O_RDWR);
   bw >> fd; // Write to file.
   bw >> std::cout; // write to standard out.

For convenience a stream operator for :code:`std::stream` is provided to make the use more natural.

.. code-block:: cpp

   std::cout << bw;
   std::cout << bw.view(); // identical effect as the previous line.

Using a :class:`BufferWriter` with :code:`printf` is straight forward by use of the sized string
format code.

.. code-block:: cpp

   ts::LocalBufferWriter<256> bw;
   bw.print("Failed to connect to {}", addr1);
   printf("%.*s\n", static_cast<int>(bw.size()), bw.data());

Alternatively the output can be null terminated in the formatting to avoid having to pass the size. ::

   ts::LocalBufferWriter<256> bw;
   printf("%s\n", bw.print("Failed to connect to {}\0", addr1).data());

When using C++ stream I/O, writing to a stream can be done without any local variables at all.

.. code-block:: cpp

   std::cout << ts::LocalBufferWriter<256>().print("Failed to connect to {}\n", addr1);

This is handy for temporary debugging messages as it avoids having to clean up local variable
declarations later, particularly when the types involved themselves require additional local
declarations (such as in this example, an IP address which would normally require a local text
buffer for conversion before printing). As noted previously this is particularly useful inside a
:code:`case` where local variables are more annoying to set up.

Reference
+++++++++

.. class:: BufferWriter

   :class:`BufferWriter` is the abstract base class which defines the basic client interface. This
   is intended to be the reference type used when passing concrete instances rather than having to
   support the distinct types.

   .. function:: BufferWriter & write(void * data, size_t length)

      Write to the buffer starting at :arg:`data` for at most :arg:`length` bytes. If there is not
      enough room to fit all the data, none is written.

   .. function:: BufferWriter & write(std::string_view str)

      Write the string :arg:`str` to the buffer. If there is not enough room to write the string no
      data is written.

   .. function:: BufferWriter & write(char c)

      Write the character :arg:`c` to the buffer. If there is no space in the buffer the character
      is not written.

   .. function:: BufferWriter & fill(size_t n)

      Increase the output size by :arg:`n` without changing the buffer contents. This is used in
      conjunction with :func:`BufferWriter::auxBuffer` after writing output to the buffer returned by
      that method. If this method is not called then such output will not be counted by
      :func:`BufferWriter::size` and will be overwritten by subsequent output.

   .. function:: char * data() const

      Return a pointer to start of the buffer.

   .. function:: size_t size() const

      Return the number of valid (written) bytes in the buffer.

   .. function:: std::string_view view() const

      Return a :code:`std::string_view` that covers the valid data in the buffer.

   .. function:: size_t remaining() const

      Return the number of available remaining bytes that could be written to the buffer.

   .. function:: size_t capacity() const

      Return the number of bytes in the buffer.

   .. function:: char * auxBuffer() const

      Return a pointer to the first byte in the buffer not yet consumed.

   .. function:: BufferWriter & clip(size_t n)

      Reduce the available space by :arg:`n` bytes.

   .. function:: BufferWriter & extend(size_t n)

      Increase the available space by :arg:`n` bytes. Extreme care must be used with this method as
      :class:`BufferWriter` will trust the argument, having no way to verify it. In general this
      should only be used after calling :func:`BufferWriter::clip` and passing the same value.
      Together these allow the buffer to be temporarily reduced to reserve space for the trailing
      element of a required pair of output strings, e.g. making sure a closing quote can be written
      even if part of the string is not.

   .. function:: bool error() const

      Return :code:`true` if the buffer has overflowed from writing, :code:`false` if not.

   .. function:: size_t extent() const

      Return the total number of bytes in all attempted writes to this buffer. This value allows a
      successful retry in case of overflow, presuming the output data doesn't change. This works
      well with the standard "try before you buy" approach of attempting to write output, counting
      the characters needed, then allocating a sufficiently sized buffer and actually writing.

   .. function:: BufferWriter & print(TextView fmt, ...)

      Print the arguments according to the format. See `bw-formatting`_.

   .. function:: template <typename ... Args> \
         BufferWriter & printv(TextView fmt, std::tuple<Args...> && args)

      Print the arguments in the tuple :arg:`args` according to the format. See `bw-formatting`_.

   .. function:: std::ostream & operator >> (std::ostream & stream) const

      Write the contents of the buffer to :arg:`stream` and return :arg:`stream`.

   .. function:: ssize_t operator >> (int fd)

      Write the contents of the buffer to file descriptor :arg:`fd` and return the number of bytes
      write (the results of the call to file :code:`write()`).

.. class:: FixedBufferWriter : public BufferWriter

   This is a class that implements :class:`BufferWriter` on a fixed buffer, passed in to the constructor.

   .. function:: FixedBufferWriter(void * buffer, size_t length)

      Construct an instance that will write to :arg:`buffer` at most :arg:`length` bytes. If more
      data is written, all data past the maximum size is discarded.

   .. function:: FixedBufferWriter & reduce(size_t n)

      Roll back the output to have :arg:`n` valid (used) bytes.

   .. function:: FixedBufferWriter & reset()

      Equivalent to :code:`reduce(0)`, provide for convenience.

   .. function:: FixedBufferWriter auxWriter(size_t reserve = 0)

      Create a new instance of :class:`FixedBufferWriter` for the remaining output buffer. If
      :arg:`reserve` is non-zero then if possible the capacity of the returned instance is reduced
      by :arg:`reserve` bytes, in effect reserving that amount of space at the end. Note the space will
      not be reserved if :arg:`reserve` is larger than the remaining output space.

.. class:: template < size_t N > LocalBufferWriter : public BufferWriter

   This is a convenience class which is a subclass of :class:`FixedBufferWriter`. It which creates a
   buffer as a member rather than having an external buffer that is passed to the instance. The
   buffer is :arg:`N` bytes long. This differs from its super class only in the constructor, which
   is only a default constructor.

   .. function:: LocalBufferWriter::LocalBufferWriter()

      Construct an instance with a capacity of :arg:`N`.

.. class:: BWFSpec

   This holds a format specifier. It has the parsing logic for a specifier and if the constructor is
   passed a :code:`std::string_view` of a specifier, that will parse it and loaded into the class
   members. This is useful to specialized implementations of :func:`bwformat`.

.. function:: template<typename V> BufferWriter& bwformat(BufferWriter & w, BWFSpec const & spec, V const & v)

   A family of overloads that perform formatted output on a :class:`BufferWriter`. The set of types
   supported can be extended by defining an overload of this function for the types.

.. function:: template < typename ... Args > \
               std::string& bwprint(std::string & s, std::string_view format, Args &&... args)

   Generate formatted output in :arg:`s` based on the :arg:`format` and arguments :arg:`args`. The
   string :arg:`s` is adjusted in size to be the exact length as required by the output. If the
   string already had enough capacity it is not re-allocated, otherwise the resizing will cause
   a re-allocation.

.. function:: template < typename ... Args > \
               std::string& bwprintv(std::string & s, std::string_view format, std::tuple<Args...> args)

   Generate formatted output in :arg:`s` based on the :arg:`format` and :arg:`args`, which must be a
   tuple of the arguments to use for the format. The string :arg:`s` is adjusted in size to be the
   exact length as required by the output. If the string already had enough capacity it is not
   re-allocated, otherwise the resizing will cause a re-allocation.

   This overload is used primarily as a back end to another function which takes the arguments for
   the formatting independently.
