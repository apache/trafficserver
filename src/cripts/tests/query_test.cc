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
#include <cripts/Preamble.hpp>

// We do this so we can have one Cript for multiple remaps
do_create_instance()
{
  if (instance.size() > 0) {
    instance.data[0] = integer(AsString(instance.data[0]));
  } else {
    instance.data[0] = 0;
  }
}

do_remap()
{
  borrow url = Client::URL::get();

  switch (AsInteger(instance.data[0])) {
  case 0:
    url.query.Erase({"foo", "bar"}, true);
    break;
  case 1:
    url.query.Erase({"foo", "bar"});
    break;
  case 2:
    url.query.Keep({"foo", "bar"});
    break;
  default:
    break;
  }

  url.query.Flush();
  CDebug("Query: {}", url.query);
}

#include <cripts/Epilogue.hpp>
