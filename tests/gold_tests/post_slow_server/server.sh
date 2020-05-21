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
# Sends a Response message with 200 Kbytes of (filler) data.  Waits two minutes after receiving the full
# Request before sending the response.  One and only parameter is the number of the TCP port to serve on.

response ()
{
  # Wait for end of Request message.
  #
  while (( 1 == 1 ))
  do
    if [[ -f rcv_file ]] ; then
      if tr '\r' '=' < rcv_file | grep '^=$' > /dev/null
      then
        break;
      fi
    fi
    sleep 1
  done

  date >&2 ; sleep 2m ; date >&2

  # Send back 200 KBytes of data

  printf "HTTP/1.1 200 OK\r\n"
  printf "Content-Length: %d\r\n" $(( 200 * 1024 ))
  printf "\r\n"

  let I=0
  while (( I < ((200 * 1024) / 8) ))
  do
    let I=I+1
    printf "%07.7u\n" $((I * 8))
  done
}

response | nc -l $1 > rcv_file
