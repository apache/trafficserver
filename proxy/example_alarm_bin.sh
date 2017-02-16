#!/bin/sh

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

#
#   Example alarm bin program. Proxy manager execs this script with
# a brief message as its argument. This program sends mail to the
# e-mail address passed in by the caller.  The subject of the
# e-mail is the passed in message, and a 'date' stamp is added
# as the body.
#
ostype=`(uname -s) 2>/dev/null`
if [ "$ostype" = "Linux" ]; then
SENDMAIL="/usr/sbin/sendmail"
else
  SENDMAIL="/usr/lib/sendmail"
fi

if [ ! -x $SENDMAIL ]; then
    echo "$0: Could not find $SENDMAIL program"
    exit 1
fi

if [ $# -eq 1 ]; then
  # if only one parameter, then no email information was provided
  msg="`hostname` $1"
  echo
  echo "[example_alarm_bin.sh] no e-mail sent: $msg"
  echo
  exit 0

elif [ $# -eq 4 ]; then
  # if four parameters, the caller specified email information
  msg="`hostname` $1"
  email_from_name=$2
  email_from_addr=$3
  email_to_addr=$4

  result=`(echo "From: $email_from_name <$email_from_addr>"; echo "To: $email_to_addr"; echo "Subject: $msg"; echo; date) | $SENDMAIL -bm $email_to_addr`
  if [ "$result" = "" ]; then
    echo
    echo "[example_alarm_bin.sh] sent alarm: $msg";
    echo
    exit 0
  else
    echo
    echo "[example_alarm_bin.sh] sendmail failed"
    echo
    exit 1
  fi

else
  # give a little help
  echo "Usage: example_alarm_bin.sh <message> [<email_from_name> <email_from_addr> <email_to_addr>]"
  exit

fi
