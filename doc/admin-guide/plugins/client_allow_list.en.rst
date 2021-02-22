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

.. _admin-plugins-client_allow_list:

Client Allow List Plugin
************************

Description
===========

The ``client allow list plugin`` checks to ensure that at least one of the
names in the client provided certificate exists in the configured allow list.
Otherwise, the TLS handshake will fail.

Allow lists can be configured in a YAML file.  The YAML file should contain one
or more key-value pairs.  Each key will be a list of one or more SNI server
names, separated by bars (|) or commas (,) .  You may also use <other> and
<none> in these server name lists.  The value for the key is one or more X509
certificate names.  if the value is just one certificate name, it can be given
as a simple YAML string value.  If there are multiple cert names, they should
be given as a YAML sequence.  A cert names can have a single asterisk (*) in it
as a wild card. The client provided certificate is allowed if its name or any
name in the signing certificate authority chain matches one of the names in the
value.  If the client sends an SNI server name in the handshake, its cert is
matched against the names for the key containing the SNI server name.  If there
are no names given for the cleint's SNI server name, the cert is matched against
the names for <other>.  If the client gives no SNI server name, the cert is
matched against the names for <none>.  When there are no names to match against,
the cert is not allowed, and the TLS handshake fails.  By default, there are no
names for <none> and <other>.

Here is an example YAML file:

.. raw:: html

  <pre>
  # They have no secrets, allow all certs.
  wikileaks.org: "*"

  # Donuts are for closer certs.
  donuts.com: closers.com

  # Suspicious SNIs.
  wearing-dark-sunglasses.com|fake-beard.com,&lt;other&gt;:
    - nuns.org
    - oprah-winfrey.com

  # Very suspicious clients, need a really good cert.
  &lt;none&gt;,pineapple-on-pizza.com: god.com

  # Fail all handshakes for this SNI server name.
  no-soup-for-you.com: ""

  aol.com|huffpost.com:
    - aol.com
    - huffpost.com
    - "*.aol.com"
    - "*.huffpost.com"
    - bernie-sanders.*.com
  </pre>

Note that cert names that start with * need to be in double quotes.  This is
because a value that starts with * is some weird YAML syntax.

The path for the YAML config file is specified as the one and only plugin
parameter.  If the path is not absolute, it is relative to the trafficserver
config dir.  The file name must have a .yaml exetension.

For simple cases, and compatibility with earlier versions of this plugin, you
don't need a YAML configuration file.  You can specify the allowed cert names
as plugin parameters.  All client certs will have to match these names,
regardless of SNI server name or if there is no SNI server name.  (You should
never use double quotes around any name in this case.)

For versions of OpenSSL prior to 1.1.1e, there are hypothetical handshake
scenarios where the SNI server name is not correctly reported by OpenSSL.
But these scenarios have not been seen to occur in a production environment so far.
