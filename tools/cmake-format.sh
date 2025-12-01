#! /usr/bin/env bash
#
#  Simple wrapper to run cmake-format on a directory.
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

# Update the these VERSION variables with the new desired cmake-version tag when
# a new cmakelang version is desired.
# See:
# https://github.com/cheshirekow/cmake_format/tags
CMAKE_FORMAT_VERSION="v0.6.13"
VERSION="0.6.13"

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

  if command -v pip3 &> /dev/null; then
    PIP_CMD="pip3"
  elif command -v pip &> /dev/null; then
    PIP_CMD="pip"
  else
    echo "pip is not installed."
    exit 1
  fi

  if ! type virtualenv >/dev/null 2>/dev/null
  then
    ${PIP_CMD} install -q virtualenv
  fi

  if python3 -m venv --help &> /dev/null; then
    VENV_LIB="venv"
  elif python3 -m virtualenv --help &> /dev/null; then
    VENV_LIB="virtualenv"
  else
    echo "Neither venv nor virtualenv is available."
    exit 1
  fi


  REPO_ROOT=$(cd $(dirname $0) && git rev-parse --show-toplevel)
  GIT_DIR=$(git rev-parse --absolute-git-dir)
  CMAKE_FORMAT_VENV=${CMAKE_FORMAT_VENV:-${GIT_DIR}/fmt/cmake_format_${CMAKE_FORMAT_VERSION}_venv}
  if [ ! -e ${CMAKE_FORMAT_VENV} ]
  then
    python3 -m ${VENV_LIB} ${CMAKE_FORMAT_VENV}
  fi
  source ${CMAKE_FORMAT_VENV}/bin/activate

  ${PIP_CMD} install -q --upgrade pip
  ${PIP_CMD} install -q "cmakelang==${CMAKE_FORMAT_VERSION}" pyaml

  ver=$(cmake-format --version 2>&1)
  if [ "$ver" != "$VERSION" ]
  then
      echo "Wrong version of cmake-format!"
      echo "Expected: \"${VERSION}\", got: \"${ver}\""
      exit 1
  fi

  DIR=${@:-.}

  # Only run cmake-format on tracked files. This saves time and possibly avoids
  # formatting files the user doesn't want formatted.
  tmp_dir=$(mktemp -d -t tracked-git-files.XXXXXXXXXX)
  files=${tmp_dir}/git_files.txt
  files_filtered=${tmp_dir}/git_files_filtered.txt
  git ls-tree -r HEAD --name-only ${DIR} | grep -E 'CMakeLists.txt|.cmake$' | grep -vE "lib/(Catch2|fastlz|ls-hpack|swoc|yamlcpp)" > ${files}
  # Add to the above any newly added staged files.
  git diff --cached --name-only --diff-filter=A >> ${files}
  # But probably not all the new staged files are CMakeLists.txt files:
  grep -E 'CMakeLists.txt|.cmake$' ${files} > ${files_filtered}
  # Prepend the filenames with "./" to make the modified file output consistent
  # with the clang-format target output.
  sed -i'.bak' 's:^:\./:' ${files_filtered}
  rm -f ${files_filtered}.bak

  # Efficiently retrieving modification timestamps in a platform
  # independent way is challenging. We use find's -newer argument, which
  # seems to be broadly supported. The following file is created and has a
  # timestamp just before running cmake-format. Any file with a timestamp
  # after this we assume was modified by cmake-format.
  start_time_file=${tmp_dir}/format_start.$$
  touch ${start_time_file}
  cmake-format  -i $(cat ${files_filtered})
  find $(cat ${files_filtered}) -newer ${start_time_file}

  rm -rf ${tmp_dir}
  deactivate
}

if [[ "$(basename -- "$0")" == 'cmake-format.sh' ]]; then
  main "$@"
else
  GIT_DIR=$(git rev-parse --absolute-git-dir)
  CMAKE_FORMAT_VENV=${CMAKE_FORMAT_VENV:-${GIT_DIR}/fmt/cmake_format_${CMAKE_FORMAT_VERSION}_venv}
fi
