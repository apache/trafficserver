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
.. default-domain:: cpp

.. _developer-plugins-building:

Building Plugins
****************

The traffic server build provides two ways to help you build your own plugins.
First, it provides a pkg-config file name ts.pc installed in `lib/pkgconfig`.
You can use this to discover include and library directories needed to build
your plugin.  The other provided option is a cmake find-package script that both
locates the traffic server libraries, but also provides some function for
building a plugin as well as verifying that the plugin produced is loadable by
traffic server.

Example Plugin Project
======================

.. code-block:: text

   cmake_minimum_required(VERSION 3.20..3.27)
   project(ats-plugin-test)

   find_package(tsapi REQUIRED)

   enable_testing()

   add_atsplugin(myplugin myplugin.cc)
   verify_remap_plugin(myplugin)

This example ``CMakeLists.txt`` finds the tsapi package which provides the
``add_atsplugin`` and ``verify_remap_plugin`` functions.
``add_atsplugin`` does all of the necessary cmake commands to build a
plugins module .so.  The function takes the plugin target name and a list of
source files that make up the project.  If the plugin requires additional
libraries, those can be linked with the ``target_link_libraries`` cmake
function.

After the plugin target is created, a verify test target can be created.  This
will add a cmake test declaration and at test time, will run traffic_server in
its verify plugin mode.  There are two verify functions provided,
``verify_remap_plugin`` and ``verify_global_plugin``.  Use the relevant
function for your plugin type (or call both if your plugin is both types).  The
test will fail if either your plugin does not define the required init function
(:func:`TSRemapInit` or :func:`TSPluginInit`), or if your plugin calls any
undefined symbols.

If traffic server is installed in a standard location, or from a package, you
might not need the -D options below, but run these commands to build and test
your plugin:

.. code-block:: text

   cmake -B build -S . -DCMAKE_MODULE_PATH=/opt/ats/lib/cmake
   -DCMAKE_INSTALL_PREFIX=/opt/ats
   cmake --build build
   cmake --build build -t test
   cmake --install build

If all of these succeed, you should be able to configure and test your new
plugin!


