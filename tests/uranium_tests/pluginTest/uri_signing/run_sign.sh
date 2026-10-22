# Script to run sign.pl script.  Single parameter is number 1 or greater selecting a set of script parameters.

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

# Preset arguments for generating tests
#
cmd_args ()
{
SELECT="$1"

FUTURE="1923056084" #  Monday, December 9, 2030 14:14:44

case "$SELECT" in
0) # future signing
  echo "-c signer.json"
  echo "-u http://somehost/someasset.ts"
	echo "--exp=${FUTURE}"
	echo "--key_index=0"
  ;;
1) # expired signing (~1970)
  echo "-c signer.json"
  echo "-u http://somehost/someasset.ts"
	echo "--key_index=0"
	echo "--exp=1"
  ;;
2) # future, second key
  echo "-c signer.json"
  echo "-u http://somehost/someasset.ts"
	echo "--exp=${FUTURE}"
	echo "--key_index=1"
  ;;
3)
  ;;
*)
  echo "run_sign.sh: bad parameter" 1>&2
  exit 1
  ;;
esac
}

# Find the path to the sign.pl script in the url_sig (source) directory.
#
find_cmd ()
{
local D T='..'
while [[ ! -d $T/.git ]]
do
  if [[ ! -d $T/.. ]] ; then
    echo "Working directory not in a git repo" 1>&2
    exit 1
  fi
  T="$T/.."
done

for D in $( find $T -name uri_signing -type d )
do
    if [[ -x $D/python_signer/uri_signer.py ]] ; then
        echo "$D/python_signer/uri_signer.py"
        return 0
    fi
done

echo "cannot find uri_signer.py" 1>&2
exit 1
}

# check for python-jose module
pip list | grep python-jose > /dev/null
status=$?
if [[ "$status" != 0 ]] ; then
    echo "cannot find python-jose" 1>&2
    exit 1
fi

CMD=$( find_cmd )
status=$?
if [[ "$status" != 0 ]] ; then
    exit 1
fi


ARGS=$( cmd_args "$1" )
status=$?
if [[ "$status" != 0 ]] ; then
    exit 1
fi

echo $CMD $ARGS

$CMD $ARGS | tr ' ' '\n' | tail -1
