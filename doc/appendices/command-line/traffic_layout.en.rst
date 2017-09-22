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

.. _traffic_cop:

traffic_layout
*****************

Description
=============
Document for the special functionality of ``runroot`` inside ``traffic_layout`` program
This feature is for the setup of traffic server runroot.
It will create a runtime sandbox for any program of traffic server to run under.

1. Use program traffic_layout to create sandbox.
2. Run any program use the sandbox with ``--run-root=/path`` or ``--run-root``

How it works:
--------------

1. Create a sandbox directory for programs to run under
2. Copy and symlink build time directories and files to sandbox, allowing users to modify freely.
3. Emit a yaml file that defines layout structure for other programs to use.

Options:
=============
1. Initialize the runroot: ::

    traffic_layout --init /path/to/sandbox/

 If no path is found, it will find the ENV variable $TS_RUNROOT

2. Remove the runroot: ::

    traffic_layout --remove /path/to/sandbox/

 Remove the sandbox we created(check yaml file).
 If no path provided, it will find the ENV variable $TS_RUNROOT.
 If $TS_RUNROOT not found, it will find bin executing path & current working directory.

3. Force flag for creating: ::

    traffic_runroot --force --init /path/to/sandbox

 Force create sandbox and overwrite existing directory 

Usage for other programs:
==============================================
Use pass in path or use Environment variable $TS_RUNROOT.
If both not found, program will try to find bin path & current woring directory. ::

    trafficserver --run-root=/path/to/runroot
    trafficserver --run-root

Notes
==========
Path to sandbox must be an absolute path.
