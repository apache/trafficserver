.. _explicit-proxy-caching:

Explicit Proxy Caching
**********************

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

If you want to use Traffic Server as an explicit proxy cache, you must
configure client software (i.e., browsers) to send requests directly to
Traffic Server.

If you do not configure Traffic Server to use the transparency option
(with which client requests are intercepted en route to origin servers
by a switch/router and rerouted to the Traffic Server machine), then
clients must configure their web browsers to send HTTP requests to the
Traffic Server proxy cache by configuring their browsers to download
proxy configuration instructions from a :ref:`PAC file <using-a-pack-file>`
(Proxy Auto-Configuration file).

Configuring Browsers Manually
=============================

To manually configure a browser to send HTTP requests to Traffic Server,
clients must provide the following information:

-  The fully-qualified hostname or IP address of the Traffic Server node
-  The Traffic Server proxy server port (by default, 8080)

In addition, clients can specify not to use Traffic Server for certain
sites - in such cases, requests to the listed sites go directly to the
origin server. The procedures for manual configuration vary among
browser versions; refer to specific browser documentation for complete
proxy configuration instructions. You do not need to set any special
configuration options on Traffic Server if you want to accept requests
from manually-configured browsers.

.. _using-a-pack-file:

Using a PAC File
================

A *PAC file* is a specialized JavaScript function definition that a
browser calls to determine how requests are handled. Clients must
specify (in their browser settings) the URL from which the PAC file is
loaded. You can store a PAC file on Traffic Server (or on any server in
your network) and then provide the URL for this file to your clients.

Sample PAC File
---------------

The following sample PAC file instructs browsers to connect directly to
all hosts without a fully-qualified domain name and to all hosts in the
local domain. All other requests go to the Traffic Server named
``myproxy.company.com``.::

    function FindProxyForURL(url, host)
    {
      if (isPlainHostName(host)) || (localHostOrDomainIs(host, ".company.com")) {
        return "DIRECT";
      }
      else
        return "PROXY myproxy.company.com:8080; DIRECT";
    }


