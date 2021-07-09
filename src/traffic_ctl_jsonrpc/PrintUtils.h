/**
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

// Record access control, indexed by RecAccessT.
static const char *
rec_accessof(int rec_access)
{
  switch (rec_access) {
  case 1:
    return "no access";
  case 2:
    return "read only";
  case 0: /* fallthrough */
  default:
    return "default";
  }
}
static const char *
rec_updateof(int rec_updatetype)
{
  switch (rec_updatetype) {
  case 1:
    return "dynamic, no restart";
  case 2:
    return "static, restart traffic_server";
  case 3:
    return "static, restart traffic_manager";
  case 0: /* fallthrough */
  default:
    return "none";
  }
}

[[maybe_unused]] static const char *
rec_checkof(int rec_checktype)
{
  switch (rec_checktype) {
  case 1:
    return "string matching a regular expression";
  case 2:
    return "integer with a specified range";
  case 3:
    return "IP address";
  case 0: /* fallthrough */
  default:
    return "none";
  }
}
static const char *
rec_labelof(int rec_class)
{
  switch (rec_class) {
  case 1:
    return "CONFIG";
  case 16:
    return "LOCAL";
  default:
    return "unknown";
  }
}
static const char *
rec_sourceof(int rec_source)
{
  switch (rec_source) {
  case 1:
    return "built in default";
  case 3:
    return "administratively set";
  case 2:
    return "plugin default";
  case 4:
    return "environment";
  default:
    return "unknown";
  }
}