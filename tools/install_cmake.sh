#! /usr/bin/env bash
#
#  Grab and install a recent version of CMake.
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

set -e

fail()
{
  echo $1
  exit 1
}
[ $# -gt 1 ] && fail "Usage: $0 [install-dir]"
if [ $# -eq 1 ]
then
  install_dir=$1
else
  install_dir=/opt
fi

set -x

os=$(uname -s)
if [ "$os" = "Linux" ]
then
  [ -d "${install_dir}" ] || fail "Install directory ${install_dir} does not exist"
  [ -w "${install_dir}" ] || fail "Install directory ${install_dir} is not writable"

  # Create and cd into a temporary directory.
  tmp_dir=$(mktemp -d -t get_cmake.XXXXXXXXXX)
  mkdir -p "${tmp_dir}"
  pushd "${tmp_dir}" > /dev/null

  if [ `uname -m` = "arm64" -o `uname -m` = "aarch64" ]; then
    arch="aarch64"
  else
    arch="x86_64"
  fi

  version=$(curl --silent "https://api.github.com/repos/Kitware/CMake/releases/latest" | awk '/tag_name/ {print $2}')

  # Remove the commas, ", and 'v' from the version string.
  version=${version//,/}
  version=${version//\"/}
  version=${version//v/}

  installer="cmake-${version}-linux-${arch}.sh"
  link="https://github.com/Kitware/CMake/releases/download/v${version}/${installer}"
  wget ${link}
  chmod +x ${installer}
  bash ${installer} --skip-license --prefix="${install_dir}"
  install_location="${install_dir}/bin/cmake"

  popd > /dev/null
  rm -rf "${tmp_dir}"

elif [ "$os" = "Darwin" ]
then
  # brew doesn't really support custom install locations. If the user specified
  # one, they'll be dissapointed if we don't use it. So we should be vocal about
  # this.
  [ $# -eq 0 ] || fail "Homebrew does not support custom install locations."
  brew install cmake
  install_location="$(brew --prefix cmake)/bin/cmake"
else
  fail "Unsupported OS: $os"
fi

# Since brew might have a different one than what we queried.
version=$(${install_location} --version | awk 'NR<=1 {print $3}')

set +x

echo
echo "------------------------------------------------------------"
echo "CMake v${version} installed in:"
echo ${install_location}
