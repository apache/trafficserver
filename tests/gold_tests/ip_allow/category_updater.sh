#!/usr/bin/env bash

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

fail()
{
  echo "$1"
  exit 1
}
[ $# -gt 0 ] || fail "Usage: $0 <filename> [localhost_category,...] [other_category,...]]"
filename="$1"
rm -f "$filename"
shift

localhost_categories=$1
shift
echo "127.0.0.1:${localhost_categories}" >> "$filename"
# Just use some random IP address for the other category.

other_categories=$1
echo "192.168.1.32:${other_categories}" >> "$filename"

