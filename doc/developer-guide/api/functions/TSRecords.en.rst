.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

Traffic Server Records
**********************

|TS| maintains a set of records which cover both configuration values and statistics.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSMgmtStringCreate(TSRecordType rec_type, const char* name, \
                                              const TSMgmtString data_default, TSRecordUpdateType update_type, \
                                              TSRecordCheckType check_type, \
                                              const char* check_regex, TSRecordAccessType access_type)


.. function:: TSReturnCode TSMgmtIntCreate(TSRecordType rec_type, const char* name, \
                                           const TSMgmtInt data_default, TSRecordUpdateType update_type, \
                                           TSRecordCheckType check_type, \
                                           const char* check_regex, TSRecordAccessType access_type)

Description
===========

:func:`TSMgmtStringCreate` registers :arg:`name` as a configuration name and sets its various properties. If :arg:`check_type` is :member:`TS_RECORDCHECK_STR` then :arg:`check_reg` must be point to a valid regular expression to use to check the string value.

If a plugin uses a value from :file:`records.config` that is not built in to |TS| it must use this function or the value will be inaccessible and a warning for that name will be generated.

Return Values
=============

:func:`TSMgmtStringCreate` and :func:`TSMgmtIntCreate` return :const:`TS_SUCCESS` if the management value was created and :const:`TS_ERROR` if not.

See Also
========

:manpage:`TSAPI(3ts)`
