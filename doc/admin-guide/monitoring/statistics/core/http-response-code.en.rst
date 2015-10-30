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

.. _admin-stats-core-http-response-code:

HTTP Response Code
******************

The statistics documented in this section provide counters for the number of
HTTP responses delivered to user agents by your |TS| instance, grouped by the
HTTP status code of the response. Please refer to the
:ref:`appendix-http-status-codes` appendix for more details on what each status
code means.

.. ts:stat:: global proxy.process.http.100_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.101_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.1xx_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.200_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.201_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.202_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.203_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.204_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.205_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.206_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.2xx_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.300_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.301_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.302_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.303_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.304_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.305_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.307_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.3xx_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.400_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.401_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.402_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.403_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.404_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.405_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.406_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.407_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.408_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.409_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.410_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.411_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.412_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.413_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.414_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.415_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.416_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.4xx_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.500_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.501_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.502_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.503_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.504_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.505_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.5xx_responses integer
   :type: counter

.. ts:stat:: global proxy.process.http.total_x_redirect_count integer
   :type: counter

