.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. default-domain:: cpp

.. highlight:: cpp

.. _remap_architecture:

Remap Architecture
******************

Remapping (or "URL rewriting") is changing the URL in the :term:`client request` to be used in the
:term:`proxy request`. Remapping is configured by creating a set of :term:`remap rule`\s and
possibly a set of :term:`remap filter`\s.

Each rule contains _parameters_ and _arguments_. Parameters are required and describe the basic
rule. On top of the parameters are arguments, which are optional and adjust the behavior of the
basic rule. These are distinguished by a leading '@' character, which marks an argument. Otherwise
it is a parameter.

Implementation
==============

.. class:: RemapArg

   An argument in a remap rule. The general form of an argument is "@tag=value".
   Each argument has a :arg:`type` which is determined by the tag. The value is stored in this
   structure, if one is present. Each type requires a value, or ignores it - there are no optional
   values.

.. class:: RemapFilter

   An access check to determine if a rule is valid for a request. The filter has a set of matching
   criteria and an action, which is either ``ALLOW`` or ``DENY``. If the filter matches the request
   the action is used, otherwise the next filter is checked.

   .. type:: List

      An :class:`IntrusiveDList` of instances.

.. class:: RemapBuilder

   This contains logic to parse the remap configuration file. This is used temporarily while building
   a new configuration. The persistent configuration data is stored in :class:`UrlRewrite` directly
   or indirectly.

.. class:: UrlRewrite

   The top level remapping structure. This is created from a configuration file and then used at run
   time to perform remapping. Data that is shared or needs to persist as long as the configuration
   is stored in this class. These are

   Localized Strings
      The string views stored in the ancillary data structures
      depend on the string being resident in this object. Such strings are "localized" in to a
      :class:`MemArena` and views are created to reference those localized copies.

   Remap Filters
      All instances of :class:`RemapFilter` for the configuation are stored here (including direct
      filters), other ancillary classes keep pointers to elements in this list.

   The rules are stored here in one of several containers. The rule type is implicit in which
   container contains the rule. It is assumed that all rules in a container have the data needed
   for the rule type of that container.

   .. class:: RegexMapping

      A container for a regular expression mapping. This contains the base mapping along with the
      regular expression and a format string. The format string is annotated with the locations of
      regular expression match group subsitutions so that if the regular expression matches, the
      results can be efficiently assembled in to the output host name.

   .. function:: TextView localize(TextView view)

      Make a local copy of :arg:`view` and return a view of that copy. The local copy is part of the
      :class:`UrlRewrite` instance and has the same lifetime. The copy is a C-string, i.e. null
      terminated although the null character is not in the returned view.

   .. member:: RemapFilter::List filters

      Configuration level storage for filters.

.. figure:: /uml/images/url-rewrite.svg
   :align: center

Memory Ownership
----------------

To avoid excessive memory allocation, the data structures for a configuration use string views to
track relevant information. These require underlying storage for the views which is embedded in the
:class:`UrlRewrite` instance, because that serves as the root of a configuration instance and therefore
controls the lifetime of the ancillary data structures containing the string views.
