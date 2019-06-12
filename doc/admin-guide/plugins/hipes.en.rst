.. _admin-plugins-hipes:

HIPES Plugin
************

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


This is a remap plugin used in the HIPES system.

Configuration
=============

``urlp:<name>``
    Default: ``url``
    Name of the query parameter for the service URL

``path:<path>``
    Default: ``/``
    Path to use for the service URL

``ssl``
    Default: ``no``
    Use SSL when connecting to the service

``service``
    Service server, ``host[:port]``

``server``
    Default: ``hipes.yimg.com``
    Name of HIPES server, ``host[:port]``

``active_timeout``
    The active connection timeout in ms

``no_activity_timeout``
    The no activity timeout in ms

``connect_timeout``
    The connect timeout in ms

``dns_timeout``
    The DNS timeout

The timeout options override the server defaults (from
```records.config``), and
only apply to the connection to the specific "service" host.

Notes on HIPES
==============

HTTP Pipes (aka HIPES and pronounced "Hippies") allows data services to be pipelined together, as illustrated by the
example below.

Example
=======
1. ATS is run on port 80 and apache HTTP web server is run on port 8080 on localhost (127.0.0.1)

2. The HIPES plugin is used in ``remap.config`` ::

    map http://127.0.0.1/svc_case http://nosuchhost @plugin=hipes.so @pparam=service:127.0.0.1:8080 @pparam=path:svc_case.php @pparam=server:127.0.0.1
    map http://127.0.0.1/svc_reverse http://nosuchhost @plugin=hipes.so @pparam=service:127.0.0.1:8080 @pparam=path:svc_reverse.php @pparam=server:127.0.0.1
    map http://127.0.0.1/test.txt http://127.0.0.1:8080/test.txt

3. The plugin remaps the incoming URL such as ::

    http://127.0.0.1/svc_reverse/svc_case;case=u/test.txt

to the following ::

    http://127.0.0.1:8080/svc_reverse?url=http%3A%2F%2F127.0.0.1%2Fsvc_case%3Bcase%3Du%2Ftest.txt

4. The service ``svc_reverse.php`` fetches the ``url`` from the ATS again and the plugin remaps the URL ::

    http://127.0.0.1/svc_case;case=u/test.txt

to this URL ::

    http://127.0.0.1:8080/svc_case.php?case=u&url=http%3A%2F%2F127.0.0.1%2Ftest.txt

5. In this example, the service ``svc_case.php`` fetches and transforms the response of ``http://127.0.0.1/test.txt``
(which ATS proxies the request to a local file) to upper case. And subsequently the service ``svc_reverse.php`` receives
the response and reverse the order before the response is sent back to the client by ATS.

Notes on reducing traffic
=========================

There can be significant overhead using HIPES because the data can traverse through ATS many times. Caching can be
important to reduce traffic to services/through ATS and can be achieved via a proper ``Cache-Control`` header returned
by the services. Another way to reduce traffic through ATS is to have ATS to return 302 redirects to url for the
requests made by service, instead of proxying the requests to that url. However, the service must then be able to follow
the redirect. The down side is that we cannot use ATS to cache intermediate results. Below is an example of using
redirect.

Modification to above example to reduce traffic using redirect
==============================================================

1. The service ``svc_reverse.php`` is modified to add a header of ``X-HIPES-Redirect: 2`` to the request made against
``url``.

2. HIPES plugin will instruct ATS to return a redirect response to this url ::

    http://127.0.0.1:8080/svc_case.php?case=u&url=http%3A%2F%2F127.0.0.1%2Ftest.txt

for the following request ::

    http://127.0.0.1/svc_case;case=u/test.txt

3.  The service ``svc_reverse.php`` is also modified to follow the redirect. Thus the response of the service of
``svc_case.php`` will not pass through ATS and will pass to ``svc_reverse.php`` service instead.

