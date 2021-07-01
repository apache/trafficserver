.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

TSRecordDataType
****************

Synopsis
========

.. code-block:: c

    #include <ts/apidefs.h>

.. c:enum:: TSRecordDataType

   The underlying data type of a data record (configuration variable or statistic).

   .. c:enumerator:: TS_RECORDDATATYPE_NULL

      No data type. Used as an invalid initialization value.

   .. c:enumerator:: TS_RECORDDATATYPE_INT

      An integer.

   .. c:enumerator:: TS_RECORDDATATYPE_FLOAT

       Floating point.

   .. c:enumerator:: TS_RECORDDATATYPE_STRING

      A string.

   .. c:enumerator:: TS_RECORDDATATYPE_COUNTER

      A counter which has a count and a sum.

   .. c:enumerator:: TS_RECORDDATATYPE_STAT_CONST

      A value that is unchangeable.

   .. c:enumerator:: TS_RECORDDATATYPE_STAT_FX

      Unknown.

.. c:union:: TSRecordData

   A union that holds the data for a record. The correct member is indicated by a :c:enum:`TSRecordType` value.

   .. c:member:: int rec_int

      Data for :c:enumerator:`TS_RECORDDATATYPE_INT <TSRecordDataType.TS_RECORDDATATYPE_INT>`.

   .. c:member:: float rec_float

      Data for :c:enumerator:`TS_RECORDDATATYPE_FLOAT <TSRecordDataType.TS_RECORDDATATYPE_FLOAT>`.

   .. c:member:: char * rec_string

      Data for :c:enumerator:`TS_RECORDDATATYPE_STRING <TSRecordDataType.TS_RECORDDATATYPE_STRING>`.

   .. c:member:: int64_t rec_counter

      Data for :c:enumerator:`TS_RECORDDATATYPE_COUNTER <TSRecordDataType.TS_RECORDDATATYPE_COUNTER>`.

Description
===========

This data type describes the data stored in a management value such as a configuration value or a
statistic value.
