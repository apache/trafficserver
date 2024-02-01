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
#!/bin/bash

if [ -z "$3" ] ; then
  echo "Usage: $0 major minor point"
  exit 1
fi

# Header
sed -i doc/conf.py --expr "s/^release = .*\$/release = \"$1.$2.$3\"/"
sed -i doc/conf.py --expr "s/^version = .*\$/version = \"$1.$2\"/"
sed -i doc/Doxyfile --expr "s/\(PROJECT_NUMBER *= *\).*\$/\\1\"$1.$2.$3\"/"

sed -i plugin/txn_box.part --expr "s/PartVersion(\"[0-9.]*\")/PartVersion(\"$1.$2.$3\")/"
