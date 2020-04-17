#! /usr/bin/env bash
#
#  Simple wrapper to run clang-format on a bunch of files
#
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

set -e # exit on error


for pr in $@; do
    if [[ "$pr" =~ ^[0-9]+$ ]]; then
	URI="https://patch-diff.githubusercontent.com/raw/apache/trafficserver/pull/${pr}.diff"
	echo "Applying changes from $URI ..."
	curl -s $URI | patch -p1
    else
	echo "$PR is not a valid pull request, skipping"
    fi
done
