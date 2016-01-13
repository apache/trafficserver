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

.. _developer-doc-building:

Building the Documentation
**************************

All documentation and related files are located in the source tree under the
``doc/`` directory. Makefiles are generated automatiically by the main configure
script. For simplicity's sake, it is recommended that contributors new to the
documentation make use of the included Vagrant configurations, as these will
take care of providing all dependencies. Please refer to the
:ref:`developer-testing-with-vagrant` chapter for complete details on using
Vagrant to build and test the |TS| source tree.

With a configured source tree, building the documentation requires only the
invocation ``make html`` from within ``doc/``. For repeated builds while working
on the documentation, it is advisable to clean out the built and intermediate
files each time by running the following instead (again, from within the ``doc/``
directory of the |TS| source tree)::

    make clean && make && make html

This will ensure that make doesn't inadvertantly skip the regeneration of any
targets.

To view the built documentation, you may point any browser to the directory
``doc/docbuild/html/``. If you are building the documentation on your local
machine, you may access the HTML documentation files directly without the need
for a full-fledged web server, as all necessary resources (CSS, Javascript, and
images) are referenced using relative paths and there are no server-side scripts
necessary to render the documentation.

