.. include:: ../../common.defs

.. _admin-plugins-cert-reporting-tool:

Cert Reporting Tool Plugin
**************************

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

Description
===========

The ``cert reporting tool`` can examine and log loaded certificates information like SAN and expiration date.
User will send plugin message to trigger the reporting/logging.

The log format for ``cert_reporting_tool.log`` is as followed:
[time] [Lookup Name] [Subject] [SAN] [serial] [NotAfter]

Plugin Configuration
====================
.. program:: cert_reporting_tool.so

* Simply put the name `cert_reporting_tool.so` in plugin.config

* ``traffic_ctl`` command.
   ``traffic_ctl plugin msg cert_reporting_tool.client 1`` - Triggers reporting/logging for client certs.

Example Usage
=============
Start |TS| with cert_reporting_tool loaded, then use traffic_ctl to send plugin message:
``traffic_ctl plugin msg cert_reporting_tool.client 1``
