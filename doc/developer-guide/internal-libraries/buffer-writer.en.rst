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

   #include <ts/BufferWriterForward.h> // Forward declarations
   #include <ts/BufferWriter.h> // Full

Description
+++++++++++

:class:`BufferWriter` is designed to make writing text to a buffer fast, convenient, and safe. It is
easier and less error-prone than using a combination of :code:`sprintf` and :code:`memcpy` as is
done in many places in the code.. A :class:`BufferWriter` can have a size and will prevent writing
past the end, while tracking the theoretical output to enable buffer resizing after the fact. This
also lets a :class:`BufferWriter` instance write into the middle of a larger buffer, making nested
output logic easy to build.

The header files are divided in to two variants. :ts:git:`lib/ts/BufferWriter.h` provides the basic
capabilities of buffer output control. :ts:git:`lib/ts/BufferWriterFormat.h` provides the basic
formatted output mechanisms, primarily the implementation and ancillary classes for
:class:`BWFSpec` which is used to build formatters.

:class:`BufferWriter` is an abstract base class, in the style of :code:`std::ostream`. There are
several subclasses for various use cases. When passing around this is the common type.

:class:`FixedBufferWriter` writes to an externally provided buffer of a fixed length. The buffer must
be provided to the constructor. This will generally be used in a function where the target buffer is
external to the function or already exists.

:class:`LocalBufferWriter` is a templated class whose template argument is the size of an internal
buffer. This is useful when the buffer is local to a function and the results will be transferred
from the buffer to other storage after the output is assembled. Rather than having code like

.. code-block:: cpp

   char buff[1024];
   ts::FixedBufferWriter w(buff, sizeof(buff));

can be more compactly and robustly done as:

.. code-block:: cpp

   ts::LocalBufferWriter<1024> w;

In many cases, when using :class:`LocalBufferWriter` this is the only place the size of the buffer
needs to be specified and therefore can simply be a constant without the overhead of defining a size
to maintain consistency. The choice between :class:`LocalBufferWriter` and :class:`FixedBufferWriter`
comes down to the owner of the buffer - the former has its own buffer while the latter operates on
a buffer owned by some other object.

Writing
-------

The basic mechanism for writing to a :class:`BufferWriter` is :func:`BufferWriter::write`.
This is an overloaded method for a character (:code:`char`), a buffer (:code:`void *, size_t`)
and a string view (:class:`string_view`). Because there is a constructor for :class:`string_view`
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
written to the buffer. This is very similar to :func:`BufferWriter::view` which returns a
:class:`string_view` which covers the output data. Calling :func:`BufferWriter::error` will indicate
if more data than space available was written. :func:`BufferWriter::extent` returns the amount of
data written to the :class:`BufferWriter`. This can be used in a two pass style with a null / size 0
buffer to determine the buffer size required for the full output.

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
is useful to do speculative output, or do bounded output in a manner similar to use
:func:`BufferWriter::clip` and :func:`BufferWriter::extend`. A new :class:`BufferWriter` instance
can be constructed with

.. code-block:: cpp

   ts::FixedBufferWriter subw(w.auxBuffer(), w.remaining());

Output can be written to :arg:`subw`. If successful, then :code:`w.fill(subw.size())` will add that
output to the main buffer. Depending on the purpose, :code:`w.fill(subw.extent())` can be used -
this will track the attempted output if sizing is important. Note that space for any terminal
markers can be reserved by bumping down the size from :func:`BufferWriter::remaining`. Be careful of
underrun as the argument is an unsigned type.

If there is an error then :arg:`subw` can be ignored and some suitable error output written to
:arg:`w` instead. A common use case is to verify there is sufficient space in the buffer and create
a "not enough space" message if not. E.g.

.. code-block:: cpp

   ts::FixedBufferWriter subw(w.auxBuffer(), w.remaining());
   this->write_some_output(subw);
   if (!subw.error()) w.fill(subw.size());
   else w << "Insufficient space"_sv;

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

   w << " [";
   if (s->txn_conf->insert_request_via_string > 2) { // Highest verbosity
      w << incoming_via;
   } else {
      w << ts::string_view{incoming_via + VIA_CLIENT, VIA_SERVER - VIA_CLIENT};
   }
   w << ']';

Note that in addition there will be no overrun on the memory buffer in :arg:`w`, in strong contrast
to the original code.

Reference
+++++++++

.. class:: BufferWriter

   :class:`BufferWriter` is the abstract base class which defines the basic client interface. This
   is intended to be the reference type used when passing concrete instances rather than having to
   support the distinct types.

   .. function:: BufferWriter & write(void * data, size_t length)

      Write to the buffer starting at :arg:`data` for at most :arg:`length` bytes. If there is not
      enough room to fit all the data, none is written.

   .. function:: BufferWriter & write(string_view str)

      Write the string :arg:`str` to the buffer. If there is not enough room to write the string no
      data is written.

   .. function:: BufferWriter & write(char c)

      Write the character :arg:`c` to the buffer. If there is no space in the buffer the character
      is not written.

   .. function:: fill(size_t n)

      Increase the output size by :arg:`n` without changing the buffer contents. This is used in
      conjuction with :func:`BufferWriter::auxBuffer` after writing output to the buffer returned by
      that method. If this method is not called then such output will not be counted by
      :func:`BufferWriter::size` and will be overwritten by subsequent output.

   .. function:: char * data() const

      Return a pointer to start of the buffer.

   .. function:: size_t size() const

      Return the number of valid (written) bytes in the buffer.

   .. function:: string_view view() const

      Return a :class:`string_view` that covers the valid data in the buffer.

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

.. class:: FixedBufferWriter : public BufferWriter

   This is a class that implements :class:`BufferWriter` on a fixed buffer, passed in to the constructor.

   .. function:: FixedBufferWriter(void * buffer, size_t length)

      Construct an instance that will write to :arg:`buffer` at most :arg:`length` bytes. If more
      data is written, all data past the maximum size is discarded.

   .. function:: reduce(size_t n)

      Roll back the output to :arg:`n` bytes. This is useful primarily for clearing the buffer by
      calling :code:`reduce(0)`.

   .. function:: FixedBufferWriter auxWriter(size_t reserve = 0)

      Create a new instance of :class:`FixedBufferWriter` for the remaining output buffer. If
      :arg:`reserve` is non-zero then if possible the capacity of the returned instance is reduced
      by :arg:`reserve` bytes, in effect reserving that amount of space at the end. Note the space will
      not be reserved if :arg:`reserve` is larger than the remaining output space.

.. class:: template < size_t N > LocalBufferWriter : public BufferWriter

   This is a convenience class which is a subclass of :class:`FixedBufferWriter`. It which creates a
   buffer as a member rather than having an external buffer that is passed to the instance. The
   buffer is :arg:`N` bytes long.

   .. function:: LocalBufferWriter::LocalBufferWriter()

      Construct an instance with a capacity of :arg:`N`.

.. class:: BWFSpec

   This holds a format specifier. It has the parsing logic for a specifier and if the constructor is
   passed a :class:`string_view` of a specifier, that will parse it and loaded into the class
   members. This is useful to specialized implementations of :func:`bwformat`.

.. function:: template<typename V> BufferWriter& bwformat(BufferWriter & w, BWFSpec const & spec, V const & v)

   A family of overloads that perform formatted output on a :class:`BufferWriter`. The set of types
   supported can be extended by defining an overload of this function for the types.

.. _bw-formatting:

Formatted Output
++++++++++++++++

:class:`BufferWriter` supports formatting output in a style similar to Python formatting via
:func:`BufferWriter::print`. This takes a format string which then controls the use of subsquent
arguments in generating out in the buffer. The basic format is divided in to three parts, separated by colons.

.. productionList:: BufferWriterFormat
   Format: "{" [name] [":" [specifier] [":" extension]] "}"
   name: index | name
   extension: <printable character except "{}">*

:arg:`name`
   The name of the argument to use. This can be a number in which case it is the zero based index of the argument to the method call. E.g. ``{0}`` means the first argument and ``{2}`` is the third argument after the format.

      ``bw.print("{0} {1}", 'a', 'b')`` => ``a b``

      ``bw.print("{1} {0}", 'a', 'b')`` => ``b a``

   The name can be omitted in which case it is treated as an index in parallel to the position in
   the format string. Only the position in the format string matters, not what names those other
   format elements may have used.

      ``bw.print("{0} {2} {}", 'a', 'b', 'c')`` => ``a c c``

      ``bw.print("{0} {2} {2}", 'a', 'b', 'c')`` => ``a c c``

   Note that an argument can be printed more than once if the name is used more than once.

      ``bw.print("{0} {} {0}", 'a', 'b')`` => ``a b a``

      ``bw.print("{0} {1} {0}", 'a', 'b')`` => ``a b a``

   Alphanumeric names refer to values in a global table. These will be described in more detail someday.

:arg:`specifier`
   Basic formatting control.

   .. productionList:: specifier
      specifier: [[fill]align][sign]["#"]["0"][[min][.precision][,max][type]]
      fill: <printable character except "{}%:"> | URI-char
      URI-char: "%" hex-digit hex-digit
      align: "<" | ">" | "=" | "^"
      sign: "+" | "-" | " "
      min: integer
      precision: integer
      max: integer
      type: "g" | "x" | "X" | "d" | "o" | "b" | "B" | "p" | "P"

   The output is placed in a field that is at least :token:`min` wide and no more than :token:`max` wide. If
   the output is less than :token:`min` then

      *  The :token:`fill` character is used for the extra space required. This can be an explicit
         character or a URI encoded one (to allow otherwise reserved characters).
      *  The output is shifted according to the :token:`align`.

         <
            Align to the left, fill to the right.

         >
            Align to the right, fill to the left.

         ^
            Align in the middle, fill to left and right.

         =
            Numerically align, putting the fill between the output and the sign character.

   The output is clipped by :token:`max` width characters or the end of the buffer. :token:`precision` is used by
   floating point values to specify the number of places of precision. The precense of the ``#`` character is used for
   integer values and causes a radix indicator to be used (one of ``0xb``, ``0``, ``0x``).

   :token:`type` is used to indicate type specific formatting. For integers it indicates the output
   radix. If ``#`` is present the radix is prefix is generated. Format types of the same letter are
   equivalent, varying only in the character case used for output. Most common, 'x' prints values in
   lower cased hexadecimal (:code:`0x1337beef`) while 'X' prints in upper case hexadecimal
   (:code:`0X1337BEEF`).

      = ===============
      b binary
      B Binary
      o octal
      d decimal
      x hexadecimal
      X Hexadecimal
      p pointer (hexadecimal address)
      P Pointer (Hexidecimal address)
      = ===============

   For several specializations the hexadecimal format is taken to indicate printing the value as if
   it were a hexidecimal value, in effect providing a hex dump of the value. This is the case for
   :class:`string_view` and therefore a hex dump of an object can be done by creating a
   :class:`string_view` covering the data and then printing it with :code:`{:x}`.

:arg:`extension`
   Text (excluding braces) that is passed to the formatting function. This can be used to provide
   extensions for specific argument types (e.g., IP addresses). The base logic ignores it but passes
   it on to the formatting function for the corresponding argument type which can then behave
   different based on the extension.

User Defined Formatting
+++++++++++++++++++++++

When an value needs to be formatted an overloaded function for type :code:`V` is called.

.. code-block:: cpp

   BufferWriter& ts::bwformat(BufferWriter& w, BWFSpec const& spec, V const& v)

This can (and should be) overloaded for user defined types. This makes it easier and cheaper to
build one overload on another by tweaking the :arg:`spec` as it passed through. The calling
framework will handle basic alignment, the overload does not need to unless the alignment
requirements are more detailed (e.g. integer alignment operations) or performance is critical.

The output stream operator :code:`operator<<` is defined to call this function with a default
constructed :code:`BWFSpec` instance.

Specialized Types
+++++++++++++++++

:class:`string_view`
   Generally the contents of the view.

   'x' or 'X'
      A hexidecimal dump of the contents of the view.

   'p' or 'P'
      The pointer and length value of the view.

:code:`sockaddr const*`
   The IP address is printed. Fill is used to fill in address segments if provided, not to the
   minimum width if specified.

   'x' or 'X'
      The address is printed in raw hex format.

   'p' or 'P'
      The address is printed as a pointer.

   The extension can be used to control which parts of the address are printed. These can be in any order,
   the output is always address, port, family. The default is the equivalent of "ap".

   'a'
      The address.

   'p'
      The port (host order).

   'f'
      The IP address family.

   E.g.

   .. code-block: cpp

      sockaddr const* addr;
      bw.print("Connecting to {0::a} on port {0::p}", addr); // no need to pass the argument twice.
      bw.print("Using address family {::f}", addr);

Futures
+++++++

A planned future extension is a variant of :class:`BufferWriter` that operates on a
:code:`MIOBuffer`. This would be very useful in many places that work with :code:`MIOBuffer`
instances, most specifically in the body factory logic.
