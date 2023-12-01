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
YAPF_VERSION="v0.32.0"
VERSION="yapf 0.32.0"

function main() {
  # check for python3
  python3 - << _END_
import sys

if sys.version_info.major < 3 or sys.version_info.minor < 8:
    exit(1)
_END_

  if [ $? = 1 ]; then
      echo "Python 3.8 or newer is not installed/enabled."
      exit 1
  fi

  set -e # exit on error

  if ! type virtualenv >/dev/null 2>/dev/null
  then
    pip install -q virtualenv
  fi

  REPO_ROOT=$(cd $(dirname $0) && git rev-parse --show-toplevel)
  YAPF_VENV=${YAPF_VENV:-${REPO_ROOT}/.git/fmt/yapf_${YAPF_VERSION}_venv}
  if [ ! -e ${YAPF_VENV} ]
  then
    python3 -m virtualenv ${YAPF_VENV}
  fi
  source ${YAPF_VENV}/bin/activate

  pip install -q --upgrade pip
  pip install -q "yapf==${YAPF_VERSION}"

  ver=$(yapf --version 2>&1)
  if [ "$ver" != "$VERSION" ]
  then
      echo "Wrong version of yapf!"
      echo "Expected: \"${VERSION}\", got: \"${ver}\""
      exit 1
  fi

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
  yapf \
      --style ${YAPF_CONFIG} \
      --parallel \
      --in-place \
      $(cat ${files_filtered})
  find $(cat ${files_filtered}) -newer ${start_time_file}

  rm -rf ${tmp_dir}
  deactivate
}

if [[ "$(basename -- "$0")" == 'yapf.sh' ]]; then
  main "$@"
else
  YAPF_VENV=${YAPF_VENV:-$(git rev-parse --show-toplevel)/.git/fmt/yapf_${YAPF_VERSION}_venv}
fi
