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
preemptively purge the object from the cache manually, or adjust the global
cache revalidation settings (such as fuzz times) used by other plugins.

Forced cache revalidations may be as specifically or loosely targeted as a
regular expression against your origin URLs permits. Thus, individual cache
objects may have rules created for them, or entire path prefixes, or even any
cache objects with a particular file extension.

Configuration
=============
.. program:: regex_revalidate.so

``Regex Revalidate`` is a global plugin and is configured via :file:`plugin.config`.
    .. option:: --config <path to revalidate rules> (also -c)

    (`required`) - specifies the file which contains the revalidation rules.
    The rule configuration file format is described below in
    `Revalidation Rules`_.  These rules are always reloaded when
    ``traffic_ctl config reload`` is invoked.

    .. option:: --log <path to log> (also -l)

    (`optional`) - specifies path to rule logging. This log is written to
    after rule changes and contains the current active ruleset.  If missing
    no log file is generated.

    .. option:: --disable-timed-reload (also -d)

    (`optional`) - default plugin behaviour is to check the revalidate
    rules file for changes every 60 seconds.  This option disables the
    checking.

``traffic_ctl`` <command>
    * ``traffic_ctl config reload`` - triggers a reload of the rules file.  If there are no changes then the loaded rules are discarded.
    * ``traffic_ctl plugin msg regex_revalidate config_reload`` - triggers a reload of the rules file apart from a full config reload.
    * ``traffic_ctl plugin msg regex_revalidate config_print`` - writes the current active ruleset to :file:`traffic.out`

Revalidation Rules
==================

Inside your revalidation rules configuration, each rule line is defined as a
regular expression followed by an integer which expresses the epoch time at
which the rule will expire::

    <regular expression> <rule expiry, as seconds since epoch> [type MISS or default STALE]

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

Type
----

By default any matching asset will have its cache lookup status changed
from HIT_FRESH to HIT_STALE.  By adding an extra keyword MISS at the end
of a line the asset will be marked MISS instead, forcing a refetch from
the parent. *Use with care* as this will increase bandwidth to the parent.
During configuration reload, any rule which changes it type will be
reloaded and treated as a new rule.

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

While new rules are added dynamically (the configuration file is checked
every 60 seconds for changes), rule lines removed from the configuration
file do not currently lead to that rule being removed from the running
plugin. To take these rules out of service the rule should be assigned a
new time in the past which will cause it to be pruned during reload phase.

Examples
========

The following rule would cause the cache object whose origin server is
``origin.tld`` and whose path is ``/images/foo.jpg`` to be revalidated by force
in |TS| until 6:47:27 AM on Saturday, November 14th, 2015 (UTC)::

    http://origin\.tld/images/foo\.jpg 1447483647

Note the escaping of the ``.`` metacharacter in the rule's regular expression.

Alternatively the following rule would case a refetch from the parent::

    http://origin\.tld/images/foo\.jpg 1447483647 MISS
