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

URL Rewrite Architecture
************************

URL rewrite or "remapping" means changing the URL used in the in the :term:`proxy request`. This is
initially the same as in the :term:`client request` and remains so if no URL rewriting is done.

Rewriting is configured by an ordered list of rules. Each rule contains _parameters_ and
_arguments_. Parameters are required and describe the basic rule. On top of the parameters are
arguments, which are optional and adjust the behavior of the basic rule. These are distinguished by
a leading '@' character, which marks an argument. Otherwise it is a parameter.

Implementation
==============

.. class:: acl_filter_rule

   An access check to determine if a rule is enabled for a request. The filter has a set of matching
   criteria and an action, which is either ``ALLOW`` or ``DENY``. If the filter matches the request
   the action is used, otherwise the next filter is checked.

.. class:: UrlRewrite

   The top level remapping structure. This is created from a configuration file and then used during
   a transaction to perform remapping. Data that is shared or needs to persist as long as the
   configuration is stored in this class. These are

   The rules are stored here in one of several containers. The rule type is implicit in which
   container contains the rule. It is assumed that all rules in a container have the data needed
   for the rule type of that container.

   .. class:: RegexMapping

      A container for a regular expression mapping. This contains the base mapping along with the
      regular expression and a format string. The format string is annotated with the locations of
      regular expression match group substitutions so that if the regular expression matches, the
      results can be efficiently assembled in to the output host name.

.. figure:: /uml/images/url_rewrite.svg
   :align: center
