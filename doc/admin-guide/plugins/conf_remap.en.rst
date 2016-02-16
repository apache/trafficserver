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

.. _admin-plugins-conf-remap:

Configuration Remap Plugin
**************************

This plugin allows you to override configuration directives dependent on
remapping rules.

Purpose
=======

|TS| provides a plethora of configuration options, but specifying the values of
those options in :file:`records.config` is global. All requests, regardless of
the cache object or its origin, will be evaluated within the same collection of
settings. Sometimes you may want |TS| to behave differently for portions of
your cache.

Perhaps you have :ref:`admin-negative-response-caching` enabled, but you wish
to greatly reduce the validity times for just one of your origin servers while
allowing the rest of your origins to have their errors cached for long
durations.

Or maybe you make use of :ref:`admin-heuristic-expiration` but require
different fuzz times for various objects because of the nature of their content
and expected lifetimes.

Any configuration directive which is overridable can be modified on a per-map
basis with this plugin. This opens up a level of flexibility in your
configurations for effectively managing and caching content with varied needs,
without having to resort to multiple |TS| instances.

Installation
============

This plugin is considered stable and is included with |TS| by default. There
are no special steps necessary for its installation.

Configuration
=============

Configuration of this plugin is performed alongside the actual remapping rules
which trigger the desired configuration directive changes. There are two
methods available for specifying the actual directives and their modified
values.

Inline Directives
-----------------

In cases where you have very few remapping rules which modify directives, and
they are modifying only a small number of directives, you may find it easiest
to simply specify those directive changes in-line with your remapping rules.
This is done by specifying *key* = *value* pairs, where the key is the
configuration directive name and the value is its desired setting for the
remapping rule.

For example, the enable :ts:cv:`proxy.config.url_remap.pristine_host_hdr` for a
single `map` rule, you would add the following to your :file:`remap.config`::

    map http://cdn.example.com/ http://origin.example.com/ \
        @plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1

External Configuration
----------------------

There may be situations in which you have many directives you wish to modify, or
where multiple remapping rules perform the same directive changes. External
configurations can simplify management of these rules, and help to reduce the
possibility of transcription errors, or keeping all the directive settings in
sync across all the remapping rules over time.

Instead of specifying the directives and their values in :file:`remap.config`
as you do with the in-line method, you place all the affected directives in a
separate text file. The location and name is entirely up to you, but we'll use
`/etc/trafficserver/cdn_conf_remap.config` here. The contents of this file
should mirror how configuration directives are written in :file:`records.config`::

    CONFIG proxy.config.url_remap.pristine_host_hdr INT 1

Your :file:`remap.config` will then contain remapping rules that point to this
external file::

    map http://cdn.example.com/ http://some-server.example.com \
        @plugin=conf_remap.so @pparam=/etc/trafficserver/cdn_conf_remap.config

Your external configuration may contain as many directives as you wish.

Caveats
=======

This plugin can only modify the values for those configuration directives which
are *overridable*, meaning they are not fixed upon |TS| startup. While this
generally shouldn't prove too onerous a restriction, you should consult the
individual directives' documentation to confirm whether they may be overridden.

Further Reading
===============

For more information about the implementation of overridable configuration
directives, you may consult the developer's documentation for
:ref:`ts-overridable-config`.

