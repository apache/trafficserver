ssl_multicert.config
********************

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

The ``ssl_multicert.config`` file lets you configure Traffic Server to
use multiple SSL server certificates with the SSL termination option. If
you have a Traffic Server system with more than one IP address assigned
to it, then you can assign a different SSL certificate to be served when
a client requests a particular IP address.

Format
======

The format of the ``ssl_multicert.config`` file is:

::

    dest_ip=ipaddress ssl_cert_name=cert_name ssl_key_name=key_name

where *``ipaddress``* is an IP address assigned to Traffic Server ,
*``ssl_cert_name``* is the filename of the Traffic Server SSL server
certificate, *``ssl_key_name``* is the filename of the Traffic Server
SSL private key. If the private key is located in the certificate file,
then you do not need to specify the name of the private key.
Additionally *``ssl_ca_name``* can be used to specify the location of a
Certification Authorithy change in case that differs from what is
specified under `:file:`records.config` <../records.config>`_'s
```proxy.config.ssl.CA.cert.filename`` <../records.config#proxy.config.ssl.CA.cert.filename>`_.

Traffic Server will try to find the files specified in
*``ssl_cert_name``* relative to
```proxy.config.ssl.server.cert.path`` <../records.config#proxy.config.ssl.server.cert.path>`_,
*``ssl_key_name``* relative to
```proxy.config.ssl.server.private_key.path`` <../records.config#proxy.config.ssl.server.private_key.path>`_,
and *``ssl_ca_name``* relative to
```proxy.config.ssl.CA.cert.path`` <../records.config#proxy.config.ssl.CA.cert.path>`_.

Examples
========

The following example configures Traffic Server to use the SSL
certificate ``server.pem`` for all requests to the IP address
111.11.11.1 and the SSL certificate ``server1.pem`` for all requests to
the IP address 11.1.1.1. Since the private key *is* included in the
certificate files, no private key name is specified.

::

    dest_ip=111.11.11.1  ssl_cert_name=server.pem
    dest_ip=11.1.1.1   ssl_cert_name=server1.pem

The following example configures Traffic Server to use the SSL
certificate ``server.pem`` and the private key ``serverKey.pem`` for all
requests to the IP address 111.11.11.1. Traffic Server uses the SSL
certificate ``server1.pem`` and the private key ``serverKey1.pem`` for
all requests to the IP address 11.1.1.1.

::

     dest_ip=111.11.11.1 ssl_cert_name=server.pem ssl_key_name=serverKey.pem
     dest_ip=11.1.1.1 ssl_cert_name=server1.pem ssl_key_name=serverKey1.pem

