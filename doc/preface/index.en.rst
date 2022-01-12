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

.. include:: ../common.defs

.. _preface:

Preface
*******

.. toctree::
  :maxdepth: 2

What is Apache Traffic Server?
==============================

|ATS| is a high-performance web proxy cache that improves network efficiency
and performance by caching frequently-accessed information at the edge of the
network. This brings content physically closer to end users, while enabling
faster delivery and reduced bandwidth use. |TS| is designed to improve content
delivery for enterprises, Internet service providers (ISPs), backbone
providers, and large intranets by maximizing existing and available bandwidth.

This manual will explore every aspect of installing, managing, extending, and
troubleshooting |TS|.

Typographic Conventions
=======================

This documentation uses the following typographic conventions:

Italic
    Used to introduce new terms on their initial appearance.

    Example:
        The |ATS| object storage is based on a *cyclone buffer* architecture.
        Cyclone buffers are a form of storage addressing in which a single
        writer continually reclaims the oldest allocations for use by new
        updates.

Monospace
    Represents C/C++ language statements, commands, file paths, file content,
    and computer output.

    Example:
        The default installation prefix for |TS| is ``/usr/local/ts``.

Bracketed Monospace
    Represents variables for which you should substitute a value in file content
    or commands.

    Example:
        Running the command ``traffic_ctl metric get <name>`` will display the current
        value of a performance statistic, where ``<name>`` is the statistic
        whose value you wish to view.

Ellipsis
    Indicates the omission of irrelevant or unimportant information.

.. _intro-other-resources:

Other Resources
===============

Websites
--------

Official Website
    https://trafficserver.apache.org/

    The official |ATS| project website is hosted by the |ASF|. Documentation,
    software downloads, community resource links, security announcements, and
    more are located, or linked to, at the site.

Online Documentation
    https://docs.trafficserver.apache.org/

    The most up to date version of the documentation is hosted at |RTD|, with
    built-in search functionality. Documentation for past releases is also
    available.

Bug Tracker
    https://github.com/apache/trafficserver/issues

    If you wish to report bugs, or look for open issues on which you may help
    contribute to the |TS| project, please visit the public bug tracker site.

Mailing Lists
-------------

User List
    The user's mailing list offers support and discussions oriented to users
    and administrators of the |TS| software.

    Send an email to ``users-subscribe@trafficserver.apache.org`` to join the
    list.

Developer List
    If you have questions about, or wish to discuss, the development of |TS|,
    plugins for the server, or other developer-oriented matters, the developer
    list offers an active list of both core project members and external
    contributors.

    Send an email to ``dev-subscribe@trafficserver.apache.org`` to join the
    list.

Slack
-----

The `traffic-server <https://the-asf.slack.com/archives/CHQ1FJ9EG>`_ channel on the |ASF| `Slack <https://infra.apache.org/slack.html>`_ is the official live chat resource for the |TS| project, and boasts active discussions.
