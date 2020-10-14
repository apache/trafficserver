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

.. highlight:: cpp
.. default-domain:: cpp

.. _jsonrpc-record-errors:

JSONRPC Record Errors
*********************

.. class:: RecordError

   .. enumerator:: RECORD_NOT_FOUND = 2000

      Record not found.
   
   .. enumerator:: RECORD_NOT_CONFIG = 2001
   
      Record is not a configuration type.
   
   .. enumerator:: RECORD_NOT_METRIC = 2002
   
      Record is not a metric type.
  
   .. enumerator:: INVALID_RECORD_NAME = 2003
      
      Invalid Record Name.
  
   .. enumerator:: VALIDITY_CHECK_ERROR = 2004
   
      Validity check failed.
  
   .. enumerator:: GENERAL_ERROR = 2005
      
      Error reading the record.
  
  .. enumerator:: RECORD_WRITE_ERROR = 2006