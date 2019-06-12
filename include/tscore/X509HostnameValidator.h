/** @file

    A partial implementation of RFC6125 for verifying that an X509 certificate matches a specific hostname.

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

#include <openssl/x509.h>

/*
 * Validate that the certificate is for the specified hostname/IP address
 * @param cert The X509 certificate we match against
 * @param hostname Null terminated string that we want to match
 * @param is_ip Is the specified hostname an IP string
 * @param peername If not NULL, the matching value from the certificate will allocated and the ptr adjusted.
 *                 In this case caller must free afterwards with ats_free
 */

bool validate_hostname(X509 *cert, const unsigned char *hostname, bool is_ip, char **peername);
