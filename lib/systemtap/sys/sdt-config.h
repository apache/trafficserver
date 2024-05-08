/** @file

  Set _SDT_ASM_SECTION_AUTOGROUP_SUPPORT to 1 to satisfy sdt.h.

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

// In the original SystemTap source code, this is configured via autoconf from a
// sdt-config.h.in file based upon whether the assembler can push a note section
// on the stack. We simply assume that all of our supported ATS platforms can do
// this.  Any platforms that do not in fact support note sections will fail the
// build in an obvious way.  A future PR, if needed, can implement something
// more intelligent here to determine at configure time whether pushing section
// notes is supported.
#define _SDT_ASM_SECTION_AUTOGROUP_SUPPORT 1
