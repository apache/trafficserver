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

.. include:: ../../../../common.defs

.. _admin-monitoring-monitoring-third-party:

Integrating Third-Party Tools
*****************************

There are many monitoring and alerting systems available, too many for us to
hope to cover every possible option here. We can, however, attempt to document
using some of the more common options.

Some of these third party monitoring applications and services are able to tap
into the extensive list of |TS| statistics, others make use of |TS| log files,
and yet others are aimed at simple health check reporting. An extensive service
monitoring configuration may take advantage of more than one service to play to
their complementary strengths. Which tool, or combination of tools, is right
for your infrastructure will likely vary.

.. toctree::
   :maxdepth: 1

   circonus.en
   logstash.en

