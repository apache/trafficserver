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

==================
How do I run TSQA?
==================
TSQA is mostly self contained (using python's virutalenv). There are currently only
two external depencies (below package names are for RHEL/Centos):
    - python-virtualenv
    - libcffi-devel

Run ``sudo make bootstrap`` to install the TSQA dependencies.

Once these two packages are available you simply need to run "make test" in this
directory to run all tests.

If you wish to run single tests you may do so by using nosetests from the
virtualenv directly-- this can be done by running something like:

    ./virtualenv/bin/nosetests tests/test_example.py


=====================
How do I write tests?
=====================
There are examples here in the trafficserver source tree (test_example.py), in
trafficserver-qa (https://github.com/apache/trafficserver-qa/tree/master/examples),
and other test cases to read through. If you have any questions please feel free
to send mail to the mailing lists, or pop onto IRC.


=====================
Where do I put tests?
=====================
At this point there aren't a lot of tests, so it may be difficult to know *where*
to put your test. The general plan is to group tests by functionality. For example,
if you have a keepalive test it should go with the rest of the keepalive tests.
In general where we put the test is a lot less important than the test itself.
So if you are confused about where to put it please write the test and submit a
patch or pull request, and someone will help you place it.
