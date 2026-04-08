#!/usr/bin/env bash
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

set -euo pipefail

source_dir=${1:-$(cd "$(dirname "$0")/.." && pwd)}
jar_path="${source_dir}/ci/apache-rat-0.13-SNAPSHOT.jar"
regex_path="${source_dir}/ci/rat-regex.txt"

if ! git -C "${source_dir}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  exec java -jar "${jar_path}" -E "${regex_path}" -d "${source_dir}"
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/ats-rat.XXXXXX")
scan_root="${tmpdir}/scan"
file_list="${tmpdir}/files.list"
trap 'rm -rf "${tmpdir}"' EXIT

mkdir -p "${scan_root}"

git -C "${source_dir}" ls-files -z --cached --others --exclude-standard -- \
  ':!:tools/http_load/http_load.c' \
  ':!:tools/http_load/timers.c' \
  ':!:tools/http_load/timers.h' >"${file_list}"

tar -C "${source_dir}" --null -T "${file_list}" -cf - | tar -C "${scan_root}" -xf -

exec java -jar "${jar_path}" -E "${regex_path}" -d "${scan_root}"
