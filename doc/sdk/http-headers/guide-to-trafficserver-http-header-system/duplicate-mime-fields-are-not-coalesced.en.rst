Duplicate MIME Fields Are Not Coalesced
***************************************

..
   Licensed to the Apache Software Foundation (ASF) under one
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

MIME headers can contain more than one MIME field with the same name.
Earlier versions of Traffic Server joined multiple fields with the same
name into one field with composite values. This behavior came at a
performance cost and caused interoperability problems with older clients
and servers. Therefore, this version of Traffic Server does not coalesce
duplicate fields.

Properly-behaving plugins should check for the presence of duplicate
fields and then iterate over the duplicate fields via
:c:func:`TSMimeHdrFieldNextDup`.
