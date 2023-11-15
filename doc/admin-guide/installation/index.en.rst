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

.. _admin-installing:

Installing Traffic Server
*************************

.. toctree::
   :maxdepth: 2

.. _admin-supported-platforms:

Supported Platforms
===================

.. _admin-traffic-server-versioning:

Traffic Server Versioning
=========================

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

.. _admin-binary-distributions:

Binary Distributions
====================

.. _admin-build-from-source:

Building From Source
====================

.. _admin-retrieving-tarballs:

Retrieving Tarballs
-------------------

Compressed archives of the source code for |TS| are available on the official
website's `Downloads <https://trafficserver.apache.org/downloads>`_ page. From
there you may select the version most appropriate for your needs. The |TS|
project does not provide binary downloads.

.. _admin-cloning-from-version-control:

Cloning from Version Control
----------------------------

|TS| uses a `public Git repository <https://git-wip-us.apache.org/repos/asf/trafficserver.git>`_
for version control. This repository will also provide the most cutting edge
source code if you wish to test out the latest features and bug fixes.

.. note::

    We do also have a `GitHub Mirror <https://github.com/apache/trafficserver>`_
    that you may use to submit pull requests. However, it may not be
    entirely up-to-date, and you should always refer to our official project
    Git repository for the very latest state of the source code.

.. _admin-build-dependencies:

Build Dependencies
------------------

In order to build Traffic Server from source you will need the following
development tools and libraries installed:

-  cmake
-  ninja
-  pkgconfig
-  gcc (>= 4.3 or clang > 3.0)
-  openssl (libssl-dev for Ubuntu 16.04)
-  pcre (libpcre3-dev for Ubuntu 16.04)
-  libcap
-  flex (for TPROXY)
-  hwloc
-  lua
-  curses (for :program:`traffic_top`)
-  curl (for :program:`traffic_top`)

If you're building from a git clone, you'll also need:

-  git
-  cmake
-  ninja
-  build-essential (for Ubuntu)

.. _admin-configuration-options:

Configuration Options
---------------------

|TS| uses ``cmake`` for building from source.  You must specify a build
directory to get started.

    cmake -B build

#. You can configure the build interactively by running ``ccmake``

    ccmake build

   By default, Traffic Server will be built to use the ``nobody`` user and group.
   You may change this by setting the ``WITH_USER`` variable::

    cmake -B build -DWITH_USER=tserver

   If dependencies are not in standard paths (``/usr/local`` or ``/usr``),
   you may need to pass options to ``cmake`` to account for that::

    cmake -B build -DCMAKE_INSTALL_PREFIX=/opt/ats luajit_ROOT=/opt/csw

   If you build |TS| with LLVM toolchain installed in optional path, you need to specify
   all of them explicitly in some cases.

    cmake -B build -DCMAKE_INSTALL_PREFIX=/opt/ats CC=/opt/bin/clang CXX=/opt/bin/clang++ LDFLAGS=-fuse-ld=lld

   To enable LTO builds, you can set the variable ``CMAKE_INTERPROCEDURAL_OPTIMIZATION`` to ``ON``

   You might be interested in using cmake presets to configure your build.
   Using presets allows you to save various configurations by name.  Look at the
   ``CMakePresets.json`` file for examples.  You can add local presets by
   creating a ``CMakeUserPresets.json`` file.  Here is an example of building
   |TS| using a preset ::

    cmake --preset default
    cmake --build build-default
    cmake --install build-default

   For more information on presets and building with cmake, please see `CMake
   Reference Documentation <https://cmake.org/cmake/help/latest/>`_

#. Once the source tree has been configured, you may proceed on to building with
   the generated Makefiles. The ``test`` target may be used to perform
   sanity checks on the resulting build, prior to installation, and it is
   recommended that you use this. ::

    cmake --build build
    cmake --build -t test

#. With the source built and checked, you may now install all of the binaries,
   header files, documentation, and other artifacts to their final locations on
   your system. ::

    sudo cmake --install build

#. Finally, it is recommended that you run the regression test suite. Please note
   that the regression tests will only be successful with the default layout. ::

    cd /opt/ats
    sudo bin/traffic_server -R 1

.. _start-traffic-server:

Start Traffic Server
====================

To start Traffic Server manually, issue the ``trafficserver`` command,
passing in the subcommand ``start``. This command starts all the
processes that work together to process Traffic Server requests as well
as manage, control, and monitor the health of the Traffic Server system. ::

   bin/trafficserver start


The :program:`traffic_ctl` provides a quick way of viewing Traffic Server statistics
and configuring the Traffic Server system via a command-line interface.

:program:`traffic_ctl` commands take the following form::

     bin/traffic_ctl COMMAND COMMAND ...

For a list of :program:`traffic_ctl` commands, enter::

     bin/traffic_ctl

Please note that :program:`traffic_ctl`, while a fine tool for administrators,
is a poor choice for automation, especially that of monitoring. See our chapter
on :ref:`monitoring <admin-monitoring>` for how to do that more efficiently and
effectively.

.. _stop-traffic-server:

Stop Traffic Server
===================

To stop Traffic Server, always use the :program:`trafficserver` command,
passing in the attribute ``stop``. This command stops all the Traffic
Server processes (:program:`traffic_server`).
Do not manually stop processes, as this can lead to unpredictable results. ::

    bin/trafficserver stop

