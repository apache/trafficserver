
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

These tools are meant to become it own repository in the future. They are here at the moment to help accelerate progress at getting everything working.

Note these Tools require python 3.4 or better.

# Traffic-Replay

Replay client to replay session logs.

Usage: 
python3.5 trafficreplay_v2/ -type <ssl|h2|random> -log_dir /path/to/log -v

Session Log format (in JSON): 

 {"version": "0.1", 
  "txns": [
        {"request": {"headers": "POST ……\r\n\r\n", "timestamp": "..", "body": ".."}, 
        "response": {"headers": "HTTP/1.1..\r\n\r\n", "timestamp": "..", "body": ".."},
         "uuid": "1"}, 
        {"request": {"headers": "POST ..….\r\n\r\n", "timestamp": "..", "body": ".."}, 
        "response": {"headers": "HTTP/1.1..\r\nr\n", "timestamp": "..", "body": ".."}, 
        "uuid": "2"}
  ], 
  "timestamp": "....", 
  "encoding": "...."}
  Configuration: The configuration required to run traffic-replay can be specified in traffic-replay/Config.py
