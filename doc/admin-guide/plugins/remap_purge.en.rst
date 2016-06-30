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

.. _admin-plugins-remap-purge:

Remap Purge Plugin
******************

This remap plugin allows the administrator to easily setup remotely
controlled ``PURGE`` for the content of an entire remap rule. The actual
purging is instant, and has no cost on the ATS server side. The
plugin makes it easy to accomplish this, since it's a simple REST
API to trigger the ``PURGE`` event.


Purpose
=======

Oftentimes, it's important, or even mission critical, to be able to purge
a large portion of an ``Apache Traffic Server``. For example, imagine that
you've pushed a new version of the site's HTML files, and you know it's going
to be hours, or even days, before the content expires in the cache.

Using this plugin, you can now easily set the ``Cache-Control`` very long,
yet expire a large portion of the cache instantly. This is building upon the
:ts:cv:`proxy.config.http.cache.generation` configuration. What the plugin
does is to provide a REST API that makes it easy to manage this generation
ID for one or many ATS servers.

Installation
============

This plugin is still experimental, but is included with |TS| when you
build with the experimental plugins enabled via ``configure``.

Configuration
=============

This plugin only functions as a remap plugin, and is therefore
configured in :file:`remap.config`. Be aware that the ``PURGE`` requests
are typically restricted to ``localhost``, but see :file:`ip_allow.config`
and :file:`remap.config` how to configure these access controls.

If PURGE does not work for your setup, you can enable a relaxed configuration
which allows GET requests to also perform the purge. This is not a recommended
configuration, since there are likely no ACLs for this now, only the secret
protects the content from being maliciously purged.

Plugin Options
--------------

There are three configuration options for this plugin::

    --secret      The secret the client sends to authorize the purge
    --header      The header the client sends the secret in (optional)
    --state-file  Name of the state file where we store the GenID
    --allow-get   This also allows a simple GET to perform the purge

Examples
--------


    map https://www.example.com http://origin.example.com \
       @plugin=purge_remap.so @pparam=--state-file=example \
                              @pparam=--header=ATS-Purger \
			      @pparam=--secret=8BFE-656DC3564C05

This setups the server to PURGE using a special header for authentication::

    curl -X PURGE -H "ATS-Purger: 8BFE-656DC3564C05" https://www.example.com


The passing of the secret as a header is option, if not specified, the
last component of the path is used instead. Example::

    map https://www.example.com/docs http://docs.example.com \
       @plugin=purge_remap.so @pparam=--state-file=example_docs \
			      @pparam=--secret=8BFE-656DC3564C05

This can now be purged with an even simpler request, but be aware that
the secret is now likely stored in access logs as well::

    curl -X PURGE https://www.example.com/docs/8BFE-656DC3564C05
