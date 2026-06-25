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

.. _binary-log-v3-format:

Self-Describing Binary Log Format (v3)
**************************************

This page specifies the on-disk format of a binary log segment, version 3, in
enough detail to implement a decoder *without* the Traffic Server source tree.
A version 3 segment is **self-describing**: every field's type is published in
the segment header, so a generic reader can decode each entry by dispatching on
a small, stable set of type codes — no embedded copy of the ATS symbol-to-type
table is required.

Motivation
==========

In version 2, a segment header carries the field *symbols* (``fmt_fieldlist``,
e.g. ``"chi,cqu,pssc"``) and a printf-style *template* (``fmt_printf``) but
**not** the field types. To decode an entry a reader had to already know the
type of each symbol, because the value encodings are only self-delimiting once
the type is known (``IP`` is variable length, for example). That coupled every
out-of-tree parser to the exact ATS build that wrote the log.

Version 3 adds one thing: a per-segment **field-type schema** that lists the
wire type of every field, in field order. Decoding then needs only the symbols
(as keys) and the schema (for types).

Segment layout
==============

A ``.blog`` file is a stream of segments, each a serialized ``LogBuffer``:

::

    LogBufferHeader            (per segment)
      cookie       = 0xaceface
      version      = 3
      format_type, byte_count, entry_count, timestamps, flags, signature
      fmt_name_offset
      fmt_fieldlist_offset      -> "chi,cqu,pssc,..."  (symbols, comma separated)
      fmt_printf_offset         -> "%<chi> %<cqu> ..."
      src_hostname_offset, log_filename_offset
      data_offset               -> first entry
      fmt_fieldtypes_offset     -> field-type schema     (NEW in v3)
    [ LogEntryHeader | field0 field1 field2 ... ]   x entry_count
      LogEntryHeader: timestamp(8) timestamp_usec(4) entry_len(4)
      fields: concatenated in fieldlist order, no per-field tags

All ``*_offset`` members are byte offsets from the start of the segment (the
address of the ``LogBufferHeader``). ``fmt_fieldtypes_offset`` is appended
**after** ``data_offset`` so that the layout through ``data_offset`` is
byte-identical to version 2; a value of ``0`` means the schema is absent (e.g.
a text-format segment, or a version 2 segment).

Field-type schema
=================

At ``fmt_fieldtypes_offset`` the segment stores:

::

    uint16_t field_count;             // == number of symbols in fmt_fieldlist
    uint8_t  type_code[field_count];  // one type code per field, in order

``type_code[i]`` is the type of the i-th field, which corresponds to the i-th
symbol in ``fmt_fieldlist`` and the i-th value in each entry. The ``uint16_t``
``field_count`` prefix is written in **host byte order**, like the rest of
``LogBufferHeader``. The blob is padded along with the header to an 8-byte
boundary.

The schema carries no independent version of its own: the segment ``version``
(``3`` here) governs this layout, so a future schema change rides the same
``LOG_SEGMENT_VERSION`` bump rather than a second, separate counter.

Stable type codes
=================

The type codes are the values of the in-tree ``LogField::Type`` enumeration,
serialized directly. They are part of the published format and are
**append-only**: codes are never renumbered or reused.

==== ========= ===========================================================
Code Name      Wire encoding
==== ========= ===========================================================
0    INVALID   Reserved. Not emitted by a correct writer; a reader that
               meets it -- or any code it does not recognize -- cannot
               determine the field length and must stop decoding the entry.
1    sINT      A single ``int64_t``, fixed 8 bytes, **host byte order**.
2    dINT      Two ``int64_t`` (16 bytes), host byte order. Used for
               values stored as two integers, e.g. HTTP version
               major/minor.
3    STRING    NUL-terminated bytes, then padded to an 8-byte boundary.
4    IP        ``uint16_t`` address family followed by a family-sized
               address, then padded to an 8-byte boundary (see below).
==== ========= ===========================================================

The code reflects how the value is *framed* on disk, i.e. how a reader walks
(or skips) it -- not what the value means. (The ``sINT``/``dINT`` names are an
ATS-internal distinction; on the wire ``sINT`` is one 8-byte integer and
``dINT`` is two consecutive ones.) How a consumer *renders* a value -- mapping
a cache-result integer to ``TCP_HIT``, or a ``dINT`` to ``1.1`` -- is layered
on top by the consumer and is not part of the wire format.

Value encodings
===============

sINT
    An ``int64_t`` occupying exactly 8 bytes, in **host byte order** (as in
    version 2). Integer values are not endianness-normalized, so a ``.blog`` is
    not portable across hosts of differing endianness; cross-architecture
    portability is future work.

dINT
    Two consecutive ``sINT`` values: 16 bytes total, in host byte order. Used
    where one log field is stored as two integers, such as an HTTP version
    (major then minor). The reference decoder renders it as a JSON array, e.g.
    ``[1,1]``; turning that into ``1.1`` is a consumer concern.

STRING
    The string bytes followed by a single NUL, then zero padding up to the next
    8-byte boundary. The on-wire length is therefore
    ``align_up(strlen + 1, 8)``. An empty/absent string is written as ``"-"``.

IP
    A ``uint16_t`` address family in host byte order, then:

    .. list-table::
       :header-rows: 1
       :widths: 30 70

       * - Family
         - Following bytes
       * - ``AF_INET`` (IPv4)
         - 4-byte ``in_addr``
       * - ``AF_INET6`` (IPv6)
         - 16-byte ``in6_addr``
       * - ``AF_UNIX``
         - fixed-size path buffer
       * - ``AF_UNSPEC`` / other
         - no address bytes

    The whole field is padded to the next 8-byte boundary. Because the length
    depends on the family byte *inside* the value, only a reader that knows the
    field is an ``IP`` (from the schema) can compute its size — which is exactly
    why the schema is required to skip or decode unknown fields safely.

Decoding an entry
=================

Given a segment, a generic decoder:

#. Reads ``field_count`` and the ``type_code[]`` array from the schema at
   ``fmt_fieldtypes_offset``.
#. Splits ``fmt_fieldlist`` into ``field_count`` comma-separated symbols
   (``LogFormat::parse_format_string()`` joins symbols with ``,``; the reference
   decoder also tolerates spaces as separators).
#. For each entry (located via ``data_offset`` and walked using
   ``LogEntryHeader::entry_len``), reads the fields left to right, using
   ``type_code[i]`` to pick the encoding above and advance the read cursor.

The reference implementation is ``log_entry_to_json()``
(``src/traffic_logcat/LogEntryJson.cc``), which renders an entry as a JSON
object using only the symbols and the schema — it does not consult the global
field table. It is exposed by :program:`traffic_logcat`'s ``-j``/``--json``
option. For example, a three-field entry decodes to:

::

    {"chi":"192.0.2.10","cqu":"GET /index.html","pssc":200}

.. note::

   Some integer fields hold coded values (cache result, hierarchy, finish
   status, etc.). The binary format stores the raw integer; mapping it to a
   mnemonic such as ``TCP_HIT`` is a presentation concern left to the consumer.

Compatibility
=============

* **New reader, old file (v3 reader, v2 file):** supported. The readers shipped
  with Traffic Server accept the inclusive version range
  ``[2, 3]`` and size the header read to the on-disk version, so a v2 segment
  (which has no ``fmt_fieldtypes_offset``) still decodes. Its ASCII output is
  produced from ``fmt_fieldlist`` + ``fmt_printf`` exactly as before.
* **Old reader, new file (v2 reader, v3 file):** a reader built before v3
  support gates on the version and will skip v3 segments. v3 logs therefore
  require tooling from a release that understands v3. As an escape hatch, a
  binary log object can be pinned to the version 2 layout with
  ``binary_log_version: 2`` in :file:`logging.yaml`, so a not-yet-upgraded
  downstream parser keeps working during a migration.
* The text/Squid/CLF ASCII output paths are unchanged: the schema is additive
  and ignored when rendering ASCII.

.. note::

   v3 does not change integer endianness: field values, the integers in
   ``LogBufferHeader`` / ``LogEntryHeader``, and the ``IP`` family word are all
   written in host byte order, as in v2. A ``.blog`` is therefore not portable
   across hosts of differing endianness; cross-architecture portability is
   future work.
