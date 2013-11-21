
.. _getting-started:

Getting Started
***************

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


.. toctree::
   :maxdepth: 2

Before you start
================

Before you get started with Traffic Server you may have to decide which
version you want to use. Traffic Server follows the `Semantic Versioning
<http://semver.org>`_ guidelines, in summary 

A version is made of a version-triplet: ``MAJOR.MINOR.PATCH``

As of v4.0.0, there are no longer any development (or unstable) releases.
All releases are considered stable and ready for production use, releases
within a major version are always upgrade compatible. More details are
available on the `Wiki page
<https://cwiki.apache.org/confluence/display/TS/New+Release+Processes>`_.

Sometimes we speak of trunk, master or HEAD, all of which are used
interchangeably: trunk or master or sometimes TIP or HEAD, refer to the
latest code in a Git Version Control System. Master is always kept releasable,
and compatible with the current major release version. Incompatible changes
are sometimes committed on a next-major release branch, for example we have
the ``5.0.x`` branch where changes incompatible with 4.x are managed.

If your distribution does not come with a prepackaged Traffic Server,
please go to `downloads </downloads>`_ to choose the version that you
consider most appropriate for yourself. If you want to really be on the
bleeding edge you can clone our `git
repository <https://git-wip-us.apache.org/repos/asf/trafficserver.git>`_.

Please note that while we do have a `GitHub
Mirror <https://github.com/apache/trafficserver>`_ that you can also use
to submit pull requests, it may not be entirely up-to-date.

Building Traffic Server
=======================

In order to build Traffic Server from source you will need the following
(development) packages:

-  pkgconfig
-  libtool
-  gcc (>= 4.3 or clang > 3.0)
-  make (GNU Make!)
-  openssl
-  tcl
-  expat
-  pcre
-  libcap
-  flex (for TPROXY)
-  hwloc
-  lua
-  curses
-  curl (both for :program:`traffic_top`)

if you're building from a git clone, you'll also need

-  git
-  autoconf
-  automake

We will show-case a build from git::

   git clone https://git-wip-us.apache.org/repos/asf/trafficserver.git

Next, we ``cd trafficserver`` and run::

   autoreconf -if

This will generate a ``configure`` file from ``configure.ac``, so now we
can run that::

   ./configure --prefix=/opt/ats

Note well, that by default Traffic Server uses the user ``nobody``, as
well as user's primary group as Traffic Server user. If you want to
change that, you can override it here::

   ./configure --prefix=/opt/ats --with-user=tserver

If dependencies are not in standard paths (``/usr/local`` or ``/usr``),
you need to pass options to ``configure`` to account for that::

   ./configure --prefix=/opt/ats --with-user=tserver --with-lua=/opt/csw

Most ``configure`` path-options accept a format of
``"INCLUDE_PATH:LIBRARY_PATH"``::

   ./configure --prefix=/opt/ats --with-user=tserver --with-lua=/opt/csw \
      --with-pcre=/opt/csw/include:/opt/csw/lib/amd64

We can run ``make`` to build the project. We highly recommend to run
``make check`` to verify the build's general sanity::

   make
   make check

We can finally run ``make install`` to install (you may have to switch
to root to do this)::

     sudo make install

We also recommend to run a regression test. Please note that this will
only work successfully with the default ``layout``::

     cd /opt/ats
     sudo bin/traffic_server -R 1

After you have installed Traffic Server on your system, you can do any
of the following:

.. _start-traffic-server:

Start Traffic Server
====================

To start Traffic Server manually, issue the ``trafficserver`` command,
passing in the attribute ``start``. This command starts all the
processes that work together to process Traffic Server requests as well
as manage, control, and monitor the health of the Traffic Server system.

To run the ``trafficserver start`` command, e.g.::

   bin/trafficserver start

At this point your server is up and running in the default configuration
of a :ref:`reverse-proxy-and-http-redirects`.

.. _start-straffic-line:

Start Traffic Line
==================

Traffic Line provides a quick way of viewing Traffic Server statistics
and configuring the Traffic Server system via command-line interface. To
execute individual commands or script multiple commands, refer to
:program:`traffic_line`.

Traffic Line commands take the following form::

     bin/traffic_line -command argument

For a list of :program:`traffic_line` commands, enter::

     bin/traffic_line -h

Please note that :program:`traffic_line`, while a fine tool for an
administrator, is a poor choice for automation, especially that of
monitoring. See our chapter on :ref:`monitoring-traffic`
for how to do that better.

.. _stop-traffic-server:

Stop Traffic Server
===================

To stop Traffic Server, always use the :program:`trafficserver` command,
passing in the attribute ``stop``. This command stops all the Traffic
Server processes (:program:`traffic_manager`, :program:`traffic_server`, and
:program:`traffic_cop`). Do not manually stop processes, as this can lead to
unpredictable results.::

    bin/trafficserver stop

