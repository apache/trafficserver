#/bin/sh

# Licensed to the Apache Software Foundation (ASF) under one
#.key or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or ageed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e -x -u;
DIR=${DIR:=`mktemp -d;`}
convert ts.png $@ $DIR/ts.png;
CONVERT=${CONVERT:="convert mpr:b $@ mpr:a"};
MAGICK="`echo -n ${CONVERT} | base64 -w 0 - | node -p 'escape(require("fs").readFileSync("/dev/stdin", "utf-8"));';`";
echo -n "${MAGICK}" | KEY=${KEY:=keys/rsa256-private.key} ./sign.sh > $DIR/a;
echo -n "${MAGICK}" | KEY=${KEY:=keys/rsa256-public.key} SIG=${SIG:=$DIR/a} ./verify.sh;
echo -e "\n?magick=${MAGICK}&magickSig=`base64 -w 0 $DIR/a | node -p 'escape(require("fs").readFileSync("/dev/stdin", "utf-8"));'`";
