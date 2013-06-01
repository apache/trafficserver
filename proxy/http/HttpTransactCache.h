/** @file

  A brief file description

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

/****************************************************************************

   HttpTransactCache.h --
   Created On      : Thu Mar 26 17:19:35 1998


 ****************************************************************************/
#if !defined (_HttpTransactCache_h_)
#define _HttpTransactCache_h_

#include "libts.h"

struct CacheHTTPInfoVector;

enum Variability_t
{
  VARIABILITY_NONE = 0,
  VARIABILITY_SOME,
  VARIABILITY_ALL
};

enum ContentEncoding
{
  NO_GZIP = 0,
  GZIP
};


struct HttpConfigParams;

class HttpTransactCache
{
public:

  /////////////////////////////////
  // content negotiation support //
  /////////////////////////////////

  static int SelectFromAlternates(CacheHTTPInfoVector * cache_vector_data, HTTPHdr * client_request, HttpConfigParams* http_config_params);

  static float calculate_quality_of_match(HttpConfigParams * http_config_params, HTTPHdr * client_request, // in
                                          HTTPHdr * obj_client_request, // in
                                          HTTPHdr * obj_origin_server_response);        // in

  static float calculate_quality_of_accept_match(MIMEField * accept_field, MIMEField * content_field);

  static float calculate_quality_of_accept_charset_match(MIMEField * accept_field,
                                                         MIMEField * content_field,
                                                         MIMEField * cached_accept_field = NULL);

  static float calculate_quality_of_accept_encoding_match(MIMEField * accept_field,
                                                          MIMEField * content_field,
                                                          MIMEField * cached_accept_field = NULL);
  static ContentEncoding match_gzip(MIMEField * accept_field);

  static float calculate_quality_of_accept_language_match(MIMEField * accept_field,
                                                          MIMEField * content_field,
                                                          MIMEField * cached_accept_field = NULL);

  ///////////////////////////////////////////////
  // variability & server negotiation routines //
  ///////////////////////////////////////////////

  static Variability_t CalcVariability(HttpConfigParams * http_config_params, HTTPHdr * client_request,
                                       HTTPHdr * obj_client_request, HTTPHdr * obj_origin_server_response );

  static HTTPStatus match_response_to_request_conditionals(HTTPHdr * ua_request, HTTPHdr * c_response);

};

#endif
