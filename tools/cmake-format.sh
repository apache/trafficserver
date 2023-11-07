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
  set -e # exit on error

  if ! type virtualenv >/dev/null 2>/dev/null
  then
    pip install -q virtualenv
  fi

  CMAKE_FORMAT_VENV=${CMAKE_FORMAT_VENV:-$(cd $(dirname $0) && git rev-parse --show-toplevel)/.git/fmt/cmake_format_${CMAKE_FORMAT_VERSION}_venv}
  if [ ! -e ${CMAKE_FORMAT_VENV} ]
  then
    virtualenv ${CMAKE_FORMAT_VENV}
  fi
  source ${CMAKE_FORMAT_VENV}/bin/activate

  pip install -q --upgrade pip
  pip install -q "cmakelang==${CMAKE_FORMAT_VERSION}" pyaml

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
  git ls-tree -r HEAD --name-only ${DIR} | grep CMakeLists.txt | grep -vE "lib/(catch2|fastlz|swoc|yamlcpp)" > ${files}
  # Add to the above any newly added staged files.
  git diff --cached --name-only --diff-filter=A >> ${files}
  # But probably not all the new staged files are CMakeLists.txt files:
  grep -E 'CMakeLists.txt' ${files} > ${files_filtered}
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
  CMAKE_FORMAT_VENV=${CMAKE_FORMAT_VENV:-$(git rev-parse --show-toplevel)/.git/fmt/cmake_format_${CMAKE_FORMAT_VERSION}_venv}
fi
