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

==========
``TSDebug``
==========

.Nm TSDebug,
.Nm TSError,
.Nm TSIsDebugTagSet,
.Nm TSDebugSpecific,
.Nm TSHttpTxnDebugSet,
.Nm TSHttpSsnDebugSet,
.Nm TSHttpTxnDebugGet,
.Nm TSHttpSsnDebugGet,
.Nm TSAssert,
.Nm TSReleaseAssert

Library
=======
Apache Traffic Server plugin API

Synopsis
========
| ``#include <ts/ts.h>``
|
| void
| ``TSDebug``\(`const char * tag`, `const char * format`, `...`\)
|
| void
| ``TSError``\(`const char * tag`, `const char * format`, `...`\)
| 
| int
| ``TSIsDebugTagSet``\(`const char * tag`\)
| 
| void
| ``TSDebugSpecific``\(`int debug_flag`, `const char * tag`, `const char * format`, `...`\)
| 
| void
| ``TSHttpTxnDebugSet``\(`TSHttpTxn txnp`, `int on`\)
| 
| void
| ``TSHttpSsnDebugSet``\(`TSHttpSsn ssn`, `int on`\)
| 
| int
| ``TSHttpTxnDebugGet``\(`TSHttpTxn txnp`\)
| 
| int
| ``TSHttpSsnDebugGet``\(`TSHttpSsn ssn`\)
| 
| void
| ``TSAssert``\(`expression`\)
| 
| void
| ``TSReleaseAssert``\(`expression`\)

Description
===========

``TSError``\(\) is similar to ``printf``\(\) except that instead
of writing the output to the C standard output, it writes output
to the Traffic Server error log.

``TSDebug``\(\) is the same as ``TSError``\(\) except that it only
logs the debug message if the given debug tag is enabled. It writes
output to the Traffic Server debug log.

``TSIsDebugSet``\(\) returns non-zero if the given debug tag is
enabled.

In debug mode, ``TSAssert``\(\) Traffic Server to prints the file
name, line number and expression, and then aborts. In release mode,
the expression is not removed but the effects of printing an error
message and aborting are.  ``TSReleaseAssert``\(\) prints an error
message and aborts in both release and debug mode.

``TSDebugSpecific``\(\) emits a debug line even if the debug tag
is turned off, as long as debug flag is enabled. This can be used
in conjuction with ``TSHttpTxnDebugSet``\(\), ``TSHttpSsnDebugSet``\(\),
``TSHttpTxnDebugGet``\(\) and ``TSHttpSsnDebugGet``\(\) to enable
debugging on specific session and transaction objects.

Examples
========

This example uses ``TSDebugSpecific``\(\) to log a message when a specific
debugging flag is enabled::

    #include <ts/ts.h>

    // Emit debug message if "tag" is enabled or the txn debug
    // flag is set.
    TSDebugSpecifc(TSHttpTxnDebugGet(txn), "tag" ,
            "Hello World from transaction %p", txn);

SEE ALSO
========
:manpage:`TSAPI(3ts)`
