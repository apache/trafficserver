/** @file Diagnostic definitions and functions.

@section license License

Licensed to the Apache Software Foundation (ASF) under one
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
*/

#pragma once

/// Severity level for diagnostics.
/// @internal Used as array indices, do not renumber or rearrange.
/// @see DiagTypes.h
enum DiagsLevel {
  DL_Diag = 0,  // process does not die
  DL_Debug,     // process does not die
  DL_Status,    // process does not die
  DL_Note,      // process does not die
  DL_Warning,   // process does not die
  DL_Error,     // process does not die
  DL_Fatal,     // causes process termination
  DL_Alert,     // causes process termination
  DL_Emergency, // causes process termination, exits with UNRECOVERABLE_EXIT
  DL_Undefined  // must be last, used for size!
};
