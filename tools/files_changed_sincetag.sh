#! /usr/bin/env bash
#
#  Outputs the list of files changed since the most recent tag
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

set -e  # exit on error

# most recent tag
# Note: this depends on the ATS tag naming convention staying the way it is
#       (lexicographically ordered)
LAST_TAG=$(git tag | tail -n 1)


usage() {
  cat << EOF
Outputs the list of files changed since a tag.

usage: ./files_changed_sincetag [-h] [--help] [-t <tag>]

OPTIONS
    -t <tag>
        Specify the tag you want to compare HEAD to. Defaults to the
        latest tag
EOF
}

# handle args
if [ -z "$1" ]; then
  TAG=${LAST_TAG}
elif [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
  usage
  exit 0
elif [ "$1" = "-t" ]; then
  TAG=${2:-${LAST_TAG}}
else
  usage
  exit 1
fi

# output list of files changed
git --no-pager diff --name-only "$TAG"
