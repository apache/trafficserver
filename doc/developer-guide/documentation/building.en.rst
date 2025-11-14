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

All documentation and related files are located in the source tree under the :ts:git:`doc`
directory. Building documentation requires enabling the build with the CMake option ``-DENABLE_DOCS=ON``
(documentation builds are disabled by default).

Additional packages required for building the documentation.

System installs
   These should be installed via your system package manager (e.g., `yum <https://fedoraproject.org/wiki/Yum>`__,
   `dnf <https://fedoraproject.org/wiki/DNF>`__, `apt`, `brew`, etc.):

   graphviz
      Graph visualization software (provides the ``dot`` command), used for diagrams in many places.

   java
      A Java runtime is required to run PlantUML for generating diagrams.

   python3
      Python 3 is required. The build system will use this to create a virtual environment.

   pipenv
      Used to manage Python dependencies. Install with ``pip3 install pipenv`` or your system package manager.

Python packages
   Python dependencies are managed automatically via `pipenv <https://docs.pipenv.org/>`__
   and the :ts:git:`doc/Pipfile`. The build system will automatically create a virtual environment
   and install all required packages (including Sphinx, sphinx-rtd-theme, sphinxcontrib-plantuml,
   sphinx-intl for internationalization, and other dependencies) when you build the documentation.

   You do not need to manually install these packages or set up pipenv yourself - the CMake build
   targets handle this automatically.

Building the documentation
==========================

With CMake configured with ``-DENABLE_DOCS=ON``, building the documentation is straightforward::

    # Configure the build (only needed once, or when changing options)
    cmake -B build -DENABLE_DOCS=ON

    # Build HTML documentation
    cmake --build build --target generate_docs

    # Build PDF documentation (Letter paper size)
    cmake --build build --target generate_pdf

    # Build PDF documentation (A4 paper size)
    cmake --build build --target generate_pdf_a4

The build system will automatically:

1. Create a pipenv virtual environment in the build directory
2. Install all required Python packages from :ts:git:`doc/Pipfile`
3. Generate the documentation

For repeated builds while working on the documentation, simply run the build command again.
The build system will detect what needs to be regenerated. To force a complete rebuild,
you can remove the build directory and reconfigure.

.. note::

   It is expected that any PR updating the documentation builds without any errors *or warnings*.
   This can be easy to miss if the full build is not done before submitting the pull request.

To view the built documentation, you may point any browser to the directory
``doc/docbuild/html/``. If you are building the documentation on your local
machine, you may access the HTML documentation files directly without the need
for a full-fledged web server, as all necessary resources (CSS, Javascript, and
images) are referenced using relative paths and there are no server-side scripts
necessary to render the documentation.
