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

.. _developer-doc-ts-markup:

|TS| Specific Markup
********************

This section covers markup used specific to the |TS| documentation and custom
domains provided by local extensions.

Types
=====

Data types are documented using the standard ``:c:type:`` markup provided by
Sphinx. Types provided by the C API should be documented in
``doc/developer-guide/api/types/``

Constants
=========

Functions
=========

Custom Domains
==============

The |TS| documentation provides a Sphinx extension, located at
``doc/ext/traffic-server.py`` which defines custom domains for various purposes.
Those domains and their usage are documented here.

Configuration Variables
-----------------------

Configuration variables are documented with the ``:ts:cv:`` domain, which takes
three required arguments (the scope, which is the literal string
:literal:`CONFIG`, the variable name, and the data type) and a fourth optional
argument (the default value). ::

    :ts:cv: <scope> <variable name> <data type> <value>

This corresponds exactly to the line in :file:`records.config`.

Definition
~~~~~~~~~~

Scope
    The scope of the variable. For configuration variables, this will always be
    the literal string :literal:`CONFIG`.

Variable Name
    The full and exact configuration variable name.

Data Type
    Indicates the data type of the variable and must be one of the following
    literal strings:

    :literal:`INT`
        Any integer value. Values may optionally be expressed with a binary
        order of magnitude suffix; e.g. :literal:`K` for *value* * 1024,
        :literal:`M` for *value* * 1024^2, :literal:`G` for *value* * 1024^3,
        or :literal:`T` for *value* * 1024^4.

    :literal:`FLOAT`
        Any floating point value.

    :literal:`STRING`
        Any alphanumeric string.

Value
    The default value of the configuration variable. It is preferable to not
    use any order of magnitude suffix, as |TS| will rewrite its configuration
    files under varioua circumstances, and when doing so it does not maintain
    those suffixes.

Options
~~~~~~~

The domain for configuration variables takes serveral options.

reloadable
   If marked the effect of the variable can be changed by reloading the |TS| configuration.

overridable
   A flag option that should be set if the variable is overridable per transaction.

units
   This takes a string option which is a description of the units for the variable. The most common
   case is to distinguish time values with units such as "seconds", "minutes", "milliseconds", etc.

deprecated
    A simple flag option which, when attached to a configuration variable, is
    used to indicate that the variable has been deprecated and should no longer
    be used or relied upon, as it may be removed at any time by future releases.

References
~~~~~~~~~~

References to configuration variables from elsewhere in the documentation should
be made using the standard domain reference markup::

    :ts:cv:`full.variable.name`

Statistics
----------

|TS| statistics are documented using the domain ``:ts:stat:``. The domain takes
three required arguments (collection, name, data type) and an option fourth
(an example value). ::

    :ts:stat: <collection> <statistic name> <data type> <example-value>

Definition
~~~~~~~~~~

Collection
    The key name of the collection in the returned JSON data from the
    :ref:`admin-plugins-stats-over-http` plugin. For most statistics, this is the
    literal sting :literal:`global`. *Required*

Statistic Name
    The exact and full name of the statistic. *Required*

Data Type
    One of the following literal string values: :literal:`integer`,
    :literal:`float`, :literal:`boolean`, or :literal:`string`. *Required*

Example Value
    A valid example of the value which may be exported by the statistic. In
    cases where the description of the statistic makes note of particular
    features of the values, such as the case of string statistics which may be
    formatted in specific ways, providing the optional example is strongly
    recommended. *Optional*

The statistics domain also supports several options which can provide even more
metadata about the statistic. These are currently:

Options
~~~~~~~

type
    Defines the type of metric exposed by the statistic. Valid values are:

    :literal:`counter`
        Numeric values which only increment based on the accumulation or
        occurrence of underlying events. Examples include the total number of
        incoming connections, or the total number of bytes transferred.

    :literal:`gauge`
        Used for moment-in-time metrics, such as the current number of |TS|
        processes running, or the current number of connections open to origin
        servers.

    :literal:`flag`
        Indicates that values for this statistic will be an integer with each
        value indicating a particular state. Most statistics of this type are
        booleans, where :literal:`1` indicates a truth or an *on* state, and
        :literal:`0` the opposite.

    :literal:`derivative`
        Statistics of this type presents values that are calculated, or derived,
        from other staistics. They do not expose a number or state gathered
        directly. Typical statistics of this type are representations of a
        statistic over a given period (e.g. average origin connections per
        second), ratio or percentage of a statistic as part of a set (e.g. the
        percentage of total dns lookups which have failed), or any other
        statistic whose computation depends on the value(s) of one or more
        other statistics.

unit
    Indicates the units of measurement that should be assumed for the given
    statistic's value.

introduced
    May be used to indicate the version of |TS| in which the statistic was first
    available. The value of this option should be a valid, human-readable
    version number string, e.g. :literal:`5.3.0`.

deprecated
    Used to indicate that the statistic is no longer supported and may be
    removed in later versions. This option may be used as a simple flag without
    any given value, or may have a value associated in which case it should be
    a valid, human-readable version number string for the |TS| release which was
    first to deprecate the statistic.

ungathered
    A simple flag option, without any associated values, indicating that while
    the statistic is included in the output of plugins like
    :ref:`admin-plugins-stats-over-http` there is no underlying data gathered
    for the statistic. If a statistic is thus marked, it should be assumed to
    be invalid or simply unimplemented.

References
~~~~~~~~~~

To reference a statistic from elsewhere in the documentation, the standard
domain reference markup should be used::

    :ts:stat:`full.statistic.name`

References should not include the collection name, data type, or any other
components aside from the statistic name.

Referencing source code
-----------------------

To reference source code from the documentation, use the following markup::

    :ts:git:`path/to/source/file`

This creates a link to Github. Sphinx does its best to pin the reference to the
current release version of |ATS|.

Avoid using hard links to Github as Github may be replaced with another host
in the future.

.. note::

    Although adding the ability to point to a specific line number would not be
    difficult, code shifts around too much and this feature would only cause
    confusion to a downstream reader. This feature was deliberately omitted.
