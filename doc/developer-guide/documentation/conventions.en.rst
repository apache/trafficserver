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

.. _developer-doc-conventions:

Conventions
***********

The conventions detailed in this chapter should be followed when modifying the
project documentation to maintain readability and consistency.

Typographic
===========

Italic
    Used to introduce new terms. Italics used solely for emphasis should be
    avoided. When introducing new terms, only the first use should be set in
    italics, and its introduction should be followed with a definition or
    description of the term.

    Example::

        The |ATS| object storage is based on a *cyclone buffer* architecture.
        Cyclone buffers are most simply described as a fixed size, but
        continually looping block of storage updated by a single writer
        process, wherein the writer continually reclaims the oldest allocations
        for the most recent object updates.

Bold
    Bold typesetting is to be reserved for section, table, and glossary
    headings and should be avoided in paragraph copy.

Monospace
    Used to indicate filesystem paths, variable names, language keywords and
    functions, and command output. Note that in the case of variables and
    functions, whenever possible references to their documentation should be
    used in place of simple monospace markup.

    Example::

        Documentation source files for |TS| are located in the ``doc/``
        directory of the project source tree.

Bracketed Monospace
    Used to indicate, within command or source code examples, variables for
    which the reader should substitute a value.

    Example::

        To examine a performance statistic of a running |TS| instance, you may
        run the comand ``traffic_line -r <name>``, replacing ``<name>`` with
        the statistic you wish to examine.

Ellipsis
    Used to indicate the omission of irrelevant or unimportant information. May
    also be used to separate matter to be treated in detail elsewhere.

Layout Styles
=============

Block Content
-------------

Notes
    Use of ``.. note::`` blocks should be sparing. In most rendered forms, the
    note block will appear quite prominently and draw the readers' eyes away
    from the surrounding copy. It is therefore advisable that the note itself
    provide enough context and detail to be read on its own, without a full
    reading of the surrounding copy.

Important Notes
    The use of ``.. important::`` callout blocks should be limited only to those
    situations in which critical information needs to be prominently displayed.
    Suitable use would include noting that resizing a cache voiume will result
    in |TS| resetting the cache contents to empty when the service is started.
    This is information that may not be obvious, or safe to assume, for the
    reader but which can significantly (and negatively) impact the use and
    performance of |TS| in their environment. Important note blocks should not
    be used for behavior or actions which generally do not have potential
    negative side effects.

Sidebars
    The use of ``.. sidebar::`` blocks in |TS| documentation should be avoided,
    and note blocks favored in their place.

Code Samples
    Content should be set within ``.. code::`` blocks whenever a full line or
    multiple lines of source code are being included in the documentation, or
    when example shell or network commands are being demonstrated. Blank lines
    may be used to separate lines of code within the block for readability, and
    all normal indentation practices should be followed. No additional markup
    should be used within the content block.

Definition Lists
    Definition lists may be used in multiple ways, not just for the term
    listings in the glossary section. They may be used to provide individual
    detailed treatment for a list of function or command arguments or where
    any series of terms need to be explained outside of the formal glossary.

Ordered Lists
    Explicityly numbered ordered lists should be avoided. |RST| provides two
    methods of marking up ordered, numbered lists, and the automatic numbering
    form should be used in all cases where surrounding paragraphs do not need
    to reference individual list entries.

Tables
------

Tabular content may be used in any situation where a selection of items, terms,
options, formats, etc. are being compared or where a grouping of the same are
being presented alongside a small number of attributes which do not require
detailed expositions.

The |TS| project documentation supplies a custom styling override which causes
cell contents to wrap within wide tables whenever possible in cases where it is
necessary to prevent the content from overflowing into the margin. If, however,
the cell content cannot be wrapped because there are no breaking spaces present
(for example, a long variable name containing only letters, periods,
underscores, dashes, and so no, but no whitespace), the table may still require
overflowing into the page margin. Whenever possible, please try to avoid the
use of tables when presenting information that will lead to this, as it greatly
hampers readibility on smaller screens, especially tablets and mobile devices.
Alternatives such as a definition list may be better suited to the content.

Tables may be marked up using any of the |RST| styles, though it is generally
easiest to maintain those using the *simple* table markup style.

Structural
==========

Common Definitions
------------------

The |TS| project documentation maintains a common definitions, abbreviations,
and shortcut listing in the file ``doc/common.defs``. This file should be
included by all |RST| source files after the standard project copyright notice.

The file should always be included using a relative path from the current file's
location. Any commonly or repeatedly used abbreviations, especialy those of
product, company, or person names, should be added to the definitions file as
useful to avoid repetitive typing and ensure accurate spellings or legal usage.

Tables of Content
-----------------

Any chapters of non-trivial scope should begin with a table of contents on the
chapter index. The ``:depth:`` option for the table of contents may be set to
a value appropriate to the amount of content in the chapter sections. A depth of
``2`` will generally provide a balance between usefully describing the contents
of the chapter without overwhelming a reader scanning for topics relevant to
their needs or interests.

Sections and Headings
---------------------

Each chapter section should be located in a separate |RST| file. Each file
should contain the standard project copyright notice first, followed by a
unique reference marker for the section.

While |RST| itself does not define a fixed ordering of section markers, |TS|
documentation source files should use the same set of single line section
markings, proceeding through the section levels without skipping. For
consistency, the following section line markers should be used in order::

    Top Level
    *********

    Second Level
    ============

    Third Level
    -----------

    Fourth Level
    ~~~~~~~~~~~~

Any section file which reaches or has need to exceed the fourth level style of
section line markings is an excellent candidate for breaking into several
smaller, and ideally more focused, |RST| source files and referenced by an
index with its own table of contents.

Footnotes and Endnotes
----------------------

Both footnotes and endnotes should be avoided. The |TS| documentation is
intended primarily for online viewing and the positioning of footnotes in the
rendered output is not always intuitive. In cases where a footnote might have
been appropriate in print-oriented material for referencing an external
resource, that reference is more ideally integrated as a standard |RST|
reference.

For more descriptive content that might have been included as a footnote, it is
less disruptive and more useful to choose between reformullating the text to
simply include the additional wording, or consider the use of an inline note
block.

Grammatical
===========


