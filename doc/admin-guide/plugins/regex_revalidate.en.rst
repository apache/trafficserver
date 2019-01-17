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

.. _admin-plugins-regex-revalidate:

Regex Revalidate Plugin
***********************

This plugin allows for the creation of rules which match regular expressions
against mapped URLs to determine if and when a cache object revalidation should
be forced.

Purpose
=======

This plugin's intended use is the selective forcing of revalidations on cache
objects which are not yet marked as stale in |TS| but which may have been
updated at the origin - without needing to alter cache control headers,
pre-emptively purge the object from the cache manually, or adjust the global
cache revalidation settings (such as fuzz times) used by other plugins.

Forced cache revalidations may be as specifically or loosely targeted as a
regular expression against your origin URLs permits. Thus, individual cache
objects may have rules created for them, or entire path prefixes, or even any
cache objects with a particular file extension.

Installation
============

To make this plugin available, you must enable experimental plugins when
building |TS|::

    ./configure --enable-experimental-plugins

Configuration
=============

This plugin is enabled via the :file:`plugin.config` configuration file, with
two required arguments: the path to a rules file, and the path to a log file::

    regex_revalidate.so -c <path to rules> -l <path to log>

The rule configuration file format is described below in `Revalidation Rules`_.

By default The plugin regularly (every 60 seconds) checks its rules configuration
file for changes and it will also check for changes when ``traffic_ctl config reload``
is run. If the file has been modified since its last scan, the contents
are read and the in-memory rules list is updated. Thus, new rules may be added and
existing ones modified without requiring a service restart.

The configuration parameter `--disable-timed-updates` or `-d` may be used to configure
the plugin to disable timed config file change checks.  With timed checks disabled,
config file changes are checked are only when ``traffic_ctl config reload`` is run.::

    regex_revalidate.so -d -c <path to rules> -l <path to log>


Revalidation Rules
==================

Inside your revalidation rules configuration, each rule line is defined as a
regular expression followed by an integer which expresses the epoch time at
which the rule will expire::

    <regular expression> <rule expiry, as seconds since epoch>

Blank lines and lines beginning with a ``#`` character are ignored.

Matching Expression
-------------------

PCRE style regular expressions are supported and should be used to match
against the complete remapped URL of cache objects (not the original
client-side URL), including protocol scheme and origin server domain.

Rule Expiration
---------------

Every rule must have an expiration associated with it. The rule expiration is
expressed as an integer of seconds since epoch (equivalent to the return value
of :manpage:`time(2)`), after which the forced revalidation will no longer
occur.

Caveats
=======

Matches Only Post-Remapping
---------------------------

The regular expressions in revalidation rules see only the final, remapped URL
in a transaction. As such, they cannot be used to distinguish between two
client-facing URLs which are mapped to the same origin object. This is due to
the fact that the plugin uses :c:data:`TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK`.

Removing Rules
--------------

While new rules are added dynamically (the configuration file is checked every
60 seconds for changes), rule lines removed from the configuration file do not
currently lead to that rule being removed from the running plugin. In these
cases, if the rule must be taken out of service, a service restart may be
necessary.

Examples
========

The following rule would cause the cache object whose origin server is
``origin.tld`` and whose path is ``/images/foo.jpg`` to be revalidated by force
in |TS| until 6:47:27 AM on Saturday, November 14th, 2015 (UTC)::

    http://origin\.tld/images/foo\.jpg 1447483647

Note the escaping of the ``.`` metacharacter in the rule's regular expression.

