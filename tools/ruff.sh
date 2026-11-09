#! /usr/bin/env bash
#
#  Simple wrapper to run Ruff on a directory.
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

# Update this version and ruff.toml when a new Ruff formatter version is
# desired. Keeping both pinned prevents updates from unexpectedly changing the tree.
# See https://github.com/astral-sh/ruff/releases.
RUFF_VERSION="0.16.6"
VERSION="ruff ${RUFF_VERSION}"

function main() {
  set -e # exit on error

  if ! command -v uv &> /dev/null; then
    echo "uv is not installed. Please install it: https://docs.astral.sh/uv/getting-started/installation/"
    exit 1
  fi

  ver=$(uv tool run --quiet ruff@${RUFF_VERSION} --version 2>&1)
  if [ "$ver" != "$VERSION" ]; then
    echo "Wrong version of Ruff!"
    echo "Expected: \"${VERSION}\", got: \"${ver}\""
    exit 1
  fi

  REPO_ROOT=$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)
  RUFF_CONFIG=${REPO_ROOT}/ruff.toml
  DIR=("${@:-.}")

  # Only run Ruff on tracked files. This saves time and avoids formatting files
  # the user does not want formatted.
  tmp_dir=$(mktemp -d -t tracked-git-files.XXXXXXXXXX)
  trap 'rm -rf "${tmp_dir}"' EXIT
  files=${tmp_dir}/git_files.txt
  files_filtered=${tmp_dir}/git_files_filtered.txt
  git ls-tree -r HEAD --name-only "${DIR[@]}" | grep -vE '^lib/' > "${files}"
  # Add to the above any newly added staged files.
  git diff --cached --name-only --diff-filter=A >> "${files}"
  # Keep this list of Python extensions synchronized with tools/git/pre-commit.
  grep -E '\.py$|\.cli\.ext$|\.test\.ext$' "${files}" > "${files_filtered}"
  # Add back in the tools Python scripts without a .py extension.
  grep -rl '^#!.*python' "${REPO_ROOT}/tools" | grep -vE '(ruff\.sh|\.py$)' | sed "s:${REPO_ROOT}/::g" >> "${files_filtered}"
  sort -u "${files_filtered}" -o "${files_filtered}"

  # Any file newer than this timestamp was modified by Ruff.
  start_time_file=${tmp_dir}/format_start.$$
  touch "${start_time_file}"
  if [ -s "${files_filtered}" ]; then
    tr '\n' '\0' < "${files_filtered}" | xargs -0 uv tool run --quiet ruff@${RUFF_VERSION} format --config "${RUFF_CONFIG}" --quiet
  fi

  find $(<"${files_filtered}") -newer "${start_time_file}" -print | sed 's:^:./:'
}

if [[ "$(basename -- "$0")" == 'ruff.sh' ]]; then
  main "$@"
fi
