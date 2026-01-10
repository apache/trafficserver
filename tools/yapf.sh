#! /usr/bin/env bash
#
#  Simple wrapper to run yapf on a directory.
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

# Update these VERSION variables with the new desired yapf tag when a new
# yapf version is desired.
# See:
# https://github.com/google/yapf/tags
YAPF_VERSION="0.43.0"
VERSION="yapf 0.43.0"

function main() {
  set -e # exit on error

  # Check for uv.
  if ! command -v uv &> /dev/null; then
    echo "uv is not installed. Please install it: https://docs.astral.sh/uv/getting-started/installation/"
    exit 1
  fi

  ver=$(uv tool run --quiet yapf@${YAPF_VERSION} --version 2>&1)
  if [ "$ver" != "$VERSION" ]
  then
      echo "Wrong version of yapf!"
      echo "Expected: \"${VERSION}\", got: \"${ver}\""
      exit 1
  fi

  REPO_ROOT=$(cd $(dirname $0) && git rev-parse --show-toplevel)

  DIR=${@:-.}

  # Only run yapf on tracked files. This saves time and possibly avoids
  # formatting files the user doesn't want formatted.
  tmp_dir=$(mktemp -d -t tracked-git-files.XXXXXXXXXX)
  files=${tmp_dir}/git_files.txt
  files_filtered=${tmp_dir}/git_files_filtered.txt
  git ls-tree -r HEAD --name-only ${DIR} | grep -vE "lib/yamlcpp" > ${files}
  # Add to the above any newly added staged files.
  git diff --cached --name-only --diff-filter=A >> ${files}
  # Keep this list of Python extensions the same with the list of
  # extensions searched for in the tools/git/pre-commit hook.
  grep -E '\.py$|\.cli.ext$|\.test.ext$' ${files} > ${files_filtered}
  # Add back in the tools Python scripts without a .py extension.
  grep -rl '#!.*python' "${REPO_ROOT}/tools" | grep -vE '(yapf.sh|.py)' | sed "s:${REPO_ROOT}/::g" >> ${files_filtered}
  # Prepend the filenames with "./" to make the modified file output consistent
  # with the clang-format target output.
  sed -i'.bak' 's:^:\./:' ${files_filtered}
  rm -f ${files_filtered}.bak

  # Efficiently retrieving modification timestamps in a platform
  # independent way is challenging. We use find's -newer argument, which
  # seems to be broadly supported. The following file is created and has a
  # timestamp just before running yapf. Any file with a timestamp
  # after this we assume was modified by yapf.
  start_time_file=${tmp_dir}/format_start.$$
  touch ${start_time_file}
  YAPF_CONFIG=${REPO_ROOT}/.style.yapf
  uv tool run --quiet yapf@${YAPF_VERSION} \
      --style ${YAPF_CONFIG} \
      --parallel \
      --in-place \
      $(cat ${files_filtered})
  find $(cat ${files_filtered}) -newer ${start_time_file}

  rm -rf ${tmp_dir}
}

if [[ "$(basename -- "$0")" == 'yapf.sh' ]]; then
  main "$@"
fi
