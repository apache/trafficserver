/*
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

// The primary include file, this has to always be included
#include <cripts/Preamble.hpp>

glb_init()
{
  CDebug("Called glb_init()");
}

glb_txn_start()
{
  CDebug("Called glb_txn_start()");
}

glb_txn_close()
{
  CDebug("Called glb_txn_close()");
}

glb_read_request()
{
  borrow req = cripts::Client::Request::Get();
  borrow url = cripts::Client::URL::Get();

  CDebug("Called glb_read_request()");
  CDebug("Host Header: {}", req["Host"]);
  CDebug("Path: {}", url.path);
}

glb_pre_remap()
{
  borrow url = cripts::Client::URL::Get();

  CDebug("Called glb_pre_remap()");
  CDebug("Client URL: {}", url.String());
}

glb_post_remap()
{
  borrow url = cripts::Client::URL::Get();

  CDebug("Called glb_post_remap()");
  CDebug("Client URL: {}", url.String());
}

glb_cache_lookup()
{
  CDebug("Called glb_cache_lookup()");
}

glb_send_request()
{
  CDebug("Called glb_send_request()");
}

glb_read_response()
{
  CDebug("Called glb_read_response()");
}

glb_send_response()
{
  CDebug("Called glb_send_response()");
}

#include <cripts/Epilogue.hpp>
