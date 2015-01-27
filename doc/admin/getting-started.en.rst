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

Before You Start
================

Before you get started with Traffic Server you may have to decide which
version you want to use. Traffic Server follows the `Semantic Versioning
<http://semver.org>`_ guidelines.

A complete version number is made of a version-triplet: ``MAJOR.MINOR.PATCH``.

As of v4.0.0, there are no longer any development (or unstable) releases.
All releases are considered stable and ready for production use. Releases
within a major version are always upgrade compatible. More details are
available on the `New Release Processes
<https://cwiki.apache.org/confluence/display/TS/Release+Process>`_ wiki
page.

Sometimes we speak of *trunk*, *master* or *HEAD*, all of which are used
interchangeably. Trunk and master, or sometimes TIP or HEAD, refer to the
latest code in a Git version control system (also referred to as a *repository*
or *Git repo*). Master is always kept releasable, and compatible with the
current major release version. Incompatible changes are sometimes committed on
a next-major release branch; for example, we have the ``5.0.x`` branch where
changes incompatible with 4.x are managed.

If your distribution does not come with a prepackaged Traffic Server,
please go to `downloads <https://trafficserver.apache.org/downloads>`_ to choose the version that you
consider most appropriate for yourself. If you want to really be on the
bleeding edge you can clone our `git
repository <https://git-wip-us.apache.org/repos/asf/trafficserver.git>`_.

.. note::

    We do also have a `GitHub Mirror <https://github.com/apache/trafficserver>`_
    that you may use to submit pull requests. However, it may not be
    entirely up-to-date, and you should always refer to our official project
    Git repository for the very latest state of the source code.

Building Traffic Server
=======================

In order to build Traffic Server from source you will need the following
development tools and libraries installed:

-  pkgconfig
-  libtool
-  gcc (>= 4.3 or clang > 3.0)
-  GNU make
-  openssl
-  tcl
-  expat
-  pcre
-  libcap
-  flex (for TPROXY)
-  hwloc
-  lua
-  curses (for :program:`traffic_top`)
-  curl (for :program:`traffic_top`)

If you're building from a git clone, you'll also need:

-  git
-  autoconf
-  automake

The following instructions demonstrate building a fresh Traffic Server from
Git sources.

#. Clone the official Git repository for Traffic Server. ::

    git clone https://git-wip-us.apache.org/repos/asf/trafficserver.git

#. Change your work directory to the newly-cloned local repository and run autoreconf. ::

    cd trafficserver/
    autoreconf -if

#. A ``configure`` script will be generated from ``configure.ac`` which may now
   be used to configure the source tree for your build. ::

    ./configure --prefix=/opt/ats

   By default, Traffic Server will be built to use the ``nobody`` user and group.
   You may change this with the ``--with-user`` argument to ``configure``::

    ./configure --prefix=/opt/ats --with-user=tserver

   If dependencies are not in standard paths (``/usr/local`` or ``/usr``),
   you may need to pass options to ``configure`` to account for that::

    ./configure --prefix=/opt/ats --with-lua=/opt/csw

   Most ``configure`` path-options accept a format of "*INCLUDE_PATH*:*LIBRARY_PATH*"::

    ./configure --prefix=/opt/ats --with-pcre=/opt/csw/include:/opt/csw/lib/amd64

#. Once the source tree has been configured, you may proceed on to building with
   the generated Makefiles. The ``make check`` command may be used to perform
   sanity checks on the resulting build, prior to installation, and it is
   recommended that you use this. ::

    make
    make check

#. With the source built and checked, you may now install all of the binaries,
   header files, documentation, and other artifacts to their final locations on
   your system. ::

    sudo make install

#. Finally, it is recommended that you run the regression test suite. Please note
   that the regression tests will only be successful with the default layout. ::

    cd /opt/ats
    sudo bin/traffic_server -R 1

.. _build-traffic-server-with-spdy:

Building Traffic Server with SPDY
=================================

Traffic Server v5.0.x and above support SPDY. The following instructions demonstrate
building a fresh Traffic Server with SPDY enabled from Git sources.

#. Clone the spdylay Git repository from tatsuhiro. ::

    git clone https://github.com/tatsuhiro-t/spdylay

#. The below steps will build spdylay library and set the PKG_CONFIG_PATH ::

    cd spdylay/
    autoreconf -if
    ./configure --prefix=/opt/spdylay
    make install
    export PKG_CONFIG_PATH=/opt/spdylay/lib/pkgconfig/

#. Finally, you can build trafficserver following the steps in the previous section along
   with an additional option --enable-spdy as below  ::

    ./configure --enable-spdy

You are now ready to configure and run your Traffic Server installation.

.. _start-traffic-server:

Start Traffic Server
====================

To start Traffic Server manually, issue the ``trafficserver`` command,
passing in the subcommand ``start``. This command starts all the
processes that work together to process Traffic Server requests as well
as manage, control, and monitor the health of the Traffic Server system. ::

   bin/trafficserver start

.. _start-straffic-line:

Start Traffic Line
==================

Traffic Line provides a quick way of viewing Traffic Server statistics
and configuring the Traffic Server system via a command-line interface. To
execute individual commands or script multiple commands, refer to
:program:`traffic_line`.

Traffic Line commands take the following form::

     bin/traffic_line -command argument

For a list of :program:`traffic_line` commands, enter::

     bin/traffic_line -h

Please note that :program:`traffic_line`, while a fine tool for an
administrator, is a poor choice for automation, especially that of
monitoring. See our chapter on :ref:`monitoring-traffic`
for how to do that more efficiently and effectively.

.. _stop-traffic-server:

Stop Traffic Server
===================

To stop Traffic Server, always use the :program:`trafficserver` command,
passing in the attribute ``stop``. This command stops all the Traffic
Server processes (:program:`traffic_manager`, :program:`traffic_server`, and
:program:`traffic_cop`). Do not manually stop processes, as this can lead to
unpredictable results. ::

    bin/trafficserver stop

