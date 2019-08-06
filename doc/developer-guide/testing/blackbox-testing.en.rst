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

.. _blackbox-testing:

Traffic Server Blackbox Testing
********************************
Traffic Server uses the Reusable Gold Testing System (`AuTest <https://bitbucket.org/autestsuite/reusable-gold-testing-system/src/master/>`_)
for functional testing. The current layout is:

::

    gold_tests/ - contains all the TSQA v4 based tests that run on the Reusable Gold Testing System (AuTest)

    tools/ - contains programs used to help with testing.


Scripts
========  
To ease the process of running Autest, there is *autest.sh* and *bootstrap.py*. 

**autest.sh** - This file is a simple wrapper that will call the Reusable Gold Testing System (Autest) program in a pipenv. 
If the pipenv is not setup, the script will prompt user the missing components. 
That will set up the Autest on most systems in a Python virtual environment. 
The wrapper adds some basic options to the command to point to the location of the tests. 
Run the script from the ``tests/`` directory followed by ``--ats-bin`` and the bin directory where ATS is located (e.g. ``~/ats/bin``)
Use ``--help`` for more details on options for running Autest.

**bootstrap.py** - This script will check for the necessary packages needed to create a pipenv that can run Autest. 
If any package is missing, the script will alert the user. 
If all packages are available, it will install a virtual environment using the provided Pipfile.


Manual Setup
=============
To run autest manually, the recommended way is to follow these steps:

1. ``pipenv install``: create the virtual environment(only needed once).
2. ``pipenv shell``: enter a shell in the virtual environment(type ``exit`` to leave the shell).
3. ``cd gold_tests``: enter the directory containing the test files.
4. ``autest --ats-bin user_ats_bin``: run autest where user_ats_bin is the bin directory in the user's ats directory.


Advanced Setup
===============
AuTest and the relevant tools can be install manually instead of using the wrapper script. 
By doing this, it is often easier to debug issues with the testing system, or the tests. 
There are two ways this can be done.

1. Run the bootstrap script then source the path with a ``source ./env-test/bin/activate`` command. At this point autest command should run without the wrapper script.
2. Make sure you install python 3.5 or better on your system. From there install these python packages (e.g. ``pip install``):
  - hyper
  - git+https://bitbucket.org/autestsuite/reusable-gold-testing-system.git 
  - `traffic-replay <https://bitbucket.org/autestsuite/trafficreplay/src/master/>`_ (This will automatically install `MicroDNS <https://bitbucket.org/autestsuite/microdns/src/master/>`_, `MicroServer <https://bitbucket.org/autestsuite/microserver/src/master/>`_, `TrafficReplayLibrary <https://bitbucket.org/autestsuite/trafficreplaylibrary/src/master/>`_, and dnslib as part of the dependencies.)


Writing Tests
==============
When writing tests, please refer to the current `documentation <https://autestsuite.bitbucket.io>`_ for general use of the Autest system.


Testing Environment
--------------------
The environment of the testing process will have a number of added environment variables to control trafficserver running the in the sandbox location correctly. 
This can be used to easily setup other commands that should run under same environment.

Autest Extensions 
------------------
Autest allows the user to create extensions to help specialize and simplify test writing for a given application domain. 

TrafficServer
~~~~~~~~~~~~~~

For TrafficServer, we have defined the following functions and objects in ``tests/gold_tests/autest-site/trafficserver.test.ext``:

- ``MakeATSProcess(obj, name, command='traffic_server', select_ports=True, enable_tls=False)``

  - name - A name for this instance of ATS
  - command - optional argument defining what process to use. Defaults to ``traffic_server``
  - select_ports - have Autest automatically select a nonSSL port to use
  - enable_tls - have Autest automatically select SSL port (``select_ports`` must be **True**)

- ``CopyConfig(file, targetname=None, process=None)``

  - file - name of the file to copy. Relative paths are relative from the test file location
  - targetname - the name of the file when copied to the correct configuration location
  - process - optional process object to use for getting path location to copy to. Only needed if the Setup object call is not in the scope of the process object created with the MakeATSProcess(...) API.

This function copies a given configuration file to the location of the trafficserver sandbox used in a test. Given a test might have more than on trafficserver instance, it can be difficult to understand the correct location to copy to. This function will deal with the details correctly.

When automatically selected, the following ports will be allocated for TS:

- port 
- portv6
- ssl_port
- admin_port - this is set even if select_port is **False**

A number of file objects are also defined to help test TrafficServer. Files that are currently defined are:

- squid.log 
- error.log
- diags.log
- records.config
- cache.config
- congestion.config
- hosting.config
- ip_allow.yaml
- logging.yaml
- parent.config
- plugin.config
- remap.config
- socks.config
- splitdns.config
- ssl_multicert.config
- storage.config
- volume.config

Example
++++++++
.. code-block:: python

  ts1 = Test.MakeATSProcess("ts1",select_ports=False)
  # uses the setup object in the scope of the process object
  ts1.Setup.ts.CopyConfig('config/records_8090.config','records.config')


Origin Server
~~~~~~~~~~~~~~

- ``Test.MakeOriginServer(name, port, s_port, ip, delay, ssl, lookup_key, clientcert, clientkey)``

  - name - A name for this instance of origin server.
  - port - option to specify the nonSSL port. If left unspecified, the port will be autoselected. 
  - s_port - option to specify the SSL port. If left unspecified, the port will be autoselected (SSL has to be True). 
  - ip - option to specify IP address. Defaults to ``127.0.0.1``.
  - delay - option to have MicroServer delay for set amount of seconds before returning response. Defaults to ``0``. 
  - ssl - option to enable SSL 
  - lookup_key - option to change the unique idenitfier that MicroServer uses to identify each transaction. Defaults to ``PATH``.
  - clientcert - path to cert used for SSL. Defaults to the included cert in ``tests/tools/microserver/ssl``.
  - clientkey - path to key used for SSL. Same default as above. 

This function returns a AuTest process object that launches the python-based Microserver. 
Microserver is a mock server which responds to client http requests. 
Microserver needs to be setup for the tests that require an origin server behind ATS. 
The server reads a JSON-formatted data file that contains request headers and the corresponding response headers. 
Microserver responds with payload if the response header contains Content-Length or Transfer-Enconding specified.

- ``Test.addResponse(filename, request_header, response_header)``

  - filename - name of the file where the request header and response header will be written to in JSON format
  - request_header - dictionary of request header
  - response_header - dictionary of response header corresponding to the request header.

This function adds the request header and response header to a file which is then read by the microserver to populate request-response map. 
The key-fields required for the header dictionary are 'headers', 'timestamp' and 'body'.

Example 
++++++++
.. code-block:: python

  # create the origin server process
  server=Test.MakeOriginServer("server")
  # define the request header and the desired response header
  request_header={"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
  # desired response form the origin server
  response_header={"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
  # addResponse adds the transaction to a file which is used by the server
  server.addResponse("sessionlog.json", request_header, response_header)


DNS
~~~~

- ``Test.MakeDNServer(name, filename, port, ip, rr, default)``

  - name - A name for this instance of MicroDNS.
  - filename - file containing zone information for MicroDNS to read from. Defaults to ``dns_file.json``
  - port - option for the DNS port. Autoselected if left unspecified.
  - ip - option for IP address. Defaults to ``127.0.0.1``
  - rr - option to enable round robining IP. Defaults to ``False``
  - default - option to specify a default IP response when MicroDNS can't find a domain:IP pair. 

- ``dns.addRecords(records, jsonFile)``

  - records - a dictionary of domain:IP mappings in the form of ``{"domain A": [IP1, IP2], "domain B": [IP3, IP4]}``
  - jsonFile - a JSON file containing domain:IP mappings

The JSON file must take the form of

.. code-block:: python

  {
    "mappings: [
        {"domain A": [IP1, IP2]},
        {"domain B": [IP3, IP4]}
    ]
  }

Example
++++++++
.. code-block:: python

  # If everything is mapped to 127.0.0.1
  dns = Test.MakeDNServer("dns", default=['127.0.0.1'])
  #------------------------------------------------------
  # Using addRecords method
  dns = Test.MakeDNServer("dns")

  dns.addRecords(records={"foo.com.":["127.0.0.1", "127.0.1.1"]})
  # AND/OR
  dns.addRecords(jsonFile="zone.json") # where zone.json is in the format described above


Condition Testing
------------------
- ``Condition.HasCurlFeature(feature)``
This function tests Curl for possible features it has been compiled with. Consult Curl documentation for possible features.

- ``Condition.PluginExists(pluginname)``
This function tests for the existence of a certain plugin in ATS. 

- ``Condition.HasATSFeature(feature)``

This function tests TrafficServer for possible features it has been compiled with. Current features you can test for are:

- TS_HAS_LIBZ
- TS_HAS_LZMA
- TS_HAS_JEMALLOC
- TS_HAS_TCMALLOC
- TS_HAS_IN6_IS_ADDR_UNSPECIFIED
- TS_HAS_BACKTRACE
- TS_HAS_PROFILER
- TS_USE_FAST_SDK
- TS_USE_DIAGS
- TS_USE_EPOLL
- TS_USE_KQUEUE
- TS_USE_PORT
- TS_USE_POSIX_CAP
- TS_USE_TPROXY
- TS_HAS_SO_MARK
- TS_HAS_IP_TOS
- TS_USE_HWLOC
- TS_USE_SET_RBIO
- TS_USE_LINUX_NATIVE_AIO
- TS_HAS_SO_PEERCRED
- TS_USE_REMOTE_UNWINDING
- TS_HAS_128BIT_CAS
- TS_HAS_TESTS
- TS_HAS_WCCP
- SPLIT_DNS


Examples
+++++++++
.. code-block:: python 

  Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_LINUX_NATIVE_AIO'),
  )

  Test.SkipUnless(
    Condition.HasCurlFeature('http2'),
  )

  Test.SkipUnless(
    Condition.PluginExists('a-plugin.so'), 
  ) 





