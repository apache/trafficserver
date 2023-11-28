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

# A very simple cleartext server for one HTTP transaction.  Does no validation of the Request message.
# Sends a fixed response message


response ()
{
  # Wait for end of Request message.
  #
  while (( 1 == 1 ))
  do
    if [[ -f $outfile ]] ; then
      if tr '\r\n' '=!' < $outfile | grep '=!=!' > /dev/null
      then
        break;
      fi
    fi
    sleep 1
  done

  printf "HTTP/1.1 200\r\nContent-length: 15\r\n\r\n"
  printf "123456789012345"

}
outfile=$2
response | nc -l $1 > "$outfile"
