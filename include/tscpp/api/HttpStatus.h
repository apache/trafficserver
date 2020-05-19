/**
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

/**
 * @file HttpStatus.h
 * @brief Contains an enumeration and printable strings for Http Status codes.
 */

#pragma once

#include <string>

namespace atscppapi
{
/**
 * An enumeration of all available Http Status Codes.
 */
enum HttpStatus {
  HTTP_STATUS_UNKNOWN = 0,

  HTTP_STATUS_CONTINUE           = 100,
  HTTP_STATUS_SWITCHING_PROTOCOL = 101,
  HTTP_STATUS_EARLY_HINTS        = 103,

  HTTP_STATUS_OK                            = 200,
  HTTP_STATUS_CREATED                       = 201,
  HTTP_STATUS_ACCEPTED                      = 202,
  HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION = 203,
  HTTP_STATUS_NO_CONTENT                    = 204,
  HTTP_STATUS_RESET_CONTENT                 = 205,
  HTTP_STATUS_PARTIAL_CONTENT               = 206,
  HTTP_STATUS_MULTI_STATUS                  = 207,
  HTTP_STATUS_ALREADY_REPORTED              = 208,
  HTTP_STATUS_IM_USED                       = 211,

  HTTP_STATUS_MULTIPLE_CHOICES   = 300,
  HTTP_STATUS_MOVED_PERMANENTLY  = 301,
  HTTP_STATUS_MOVED_TEMPORARILY  = 302,
  HTTP_STATUS_SEE_OTHER          = 303,
  HTTP_STATUS_NOT_MODIFIED       = 304,
  HTTP_STATUS_USE_PROXY          = 305,
  HTTP_STATUS_TEMPORARY_REDIRECT = 307,
  HTTP_STATUS_PERMANENT_REDIRECT = 308,

  HTTP_STATUS_BAD_REQUEST                     = 400,
  HTTP_STATUS_UNAUTHORIZED                    = 401,
  HTTP_STATUS_PAYMENT_REQUIRED                = 402,
  HTTP_STATUS_FORBIDDEN                       = 403,
  HTTP_STATUS_NOT_FOUND                       = 404,
  HTTP_STATUS_METHOD_NOT_ALLOWED              = 405,
  HTTP_STATUS_NOT_ACCEPTABLE                  = 406,
  HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED   = 407,
  HTTP_STATUS_REQUEST_TIMEOUT                 = 408,
  HTTP_STATUS_CONFLICT                        = 409,
  HTTP_STATUS_GONE                            = 410,
  HTTP_STATUS_LENGTH_REQUIRED                 = 411,
  HTTP_STATUS_PRECONDITION_FAILED             = 412,
  HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE        = 413,
  HTTP_STATUS_REQUEST_URI_TOO_LONG            = 414,
  HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE          = 415,
  HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE = 416,
  HTTP_STATUS_EXPECTATION_FAILED              = 417,
  HTTP_STATUS_UNPROCESSABLE_ENTITY            = 422,
  HTTP_STATUS_LOCKED                          = 423,
  HTTP_STATUS_FAILED_DEPENDENCY               = 424,
  HTTP_STATUS_UPGRADE_REQUIRED                = 426,
  HTTP_STATUS_PRECONDITION_REQUIRED           = 428,
  HTTP_STATUS_TOO_MANY_REQUESTS               = 429,
  HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
  HTTP_STATUS_UNAVAILABLE_FOR_LEGAL_REASONS   = 451,

  HTTP_STATUS_INTERNAL_SERVER_ERROR           = 500,
  HTTP_STATUS_NOT_IMPLEMENTED                 = 501,
  HTTP_STATUS_BAD_GATEWAY                     = 502,
  HTTP_STATUS_SERVICE_UNAVAILABLE             = 503,
  HTTP_STATUS_GATEWAY_TIMEOUT                 = 504,
  HTTP_STATUS_HTTPVER_NOT_SUPPORTED           = 505,
  HTTP_STATUS_VARIANT_ALSO_NEGOTIATES         = 506,
  HTTP_STATUS_INSUFFICIENT_STORAGE            = 507,
  HTTP_STATUS_LOOP_DETECTED                   = 508,
  HTTP_STATUS_NOT_EXTENDED                    = 510,
  HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED = 511

};
} // namespace atscppapi
