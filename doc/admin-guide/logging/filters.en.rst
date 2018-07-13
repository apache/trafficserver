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

.. _admin-logging-filters:

Log Filters
***********

Filters, configured in :file:`logging.yaml` allow you to create rules which
may be applied to log definitions, limiting the entries included in the log
output. This may be useful if your |TS| nodes receive many events which you
have no need to log or analyze, or you wish to establish separate logs with
their own rotation policies to more rapidly perform log analysis for a subset
of events.

Configuration options are covered in detail in the
:ref:`admin-custom-logs-filters` section of :file:`logging.yaml`, so this
page is currently left as a reference to the feature's existence.
