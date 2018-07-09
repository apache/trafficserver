#!/usr/bin/env bash
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if [[ ! -r "$1" ]]
then
    echo "Expecting sphinx-build executable at '$1' but could not read it" >&2
    exit 1
fi

# We must grab the interpreter directive from the sphinx-build script to discover which python installation should be used to check
# the version.
sphinx_build_PYTHON="$(head -n 1 "$1" | sed 's;^#! *;;')"

if [[ ! -x "$sphinx_build_PYTHON" ]]
then
    echo "sphinx-build found at '$1' needs missing '$sphinx_build_PYTHON'" >&2
    return 1
fi

if [[ ! -r "$2" ]]
then
    echo "Expecting sphinx version check script at '$1' but could not read it" >&2
    exit 1
fi

"$sphinx_build_PYTHON" "$2" --check-version
