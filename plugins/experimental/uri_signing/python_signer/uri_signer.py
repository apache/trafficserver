#!/usr/bin/env python3

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

import json
import argparse
import random
import time

# https://github.com/mpdavis/python-jose
from jose import jwt

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--config',
                        help="Configuration File",
                        required=True)
    parser.add_argument('-u', '--uri',
                        help="URI to sign",
                        required=True)
    args = parser.parse_args()

    with open(args.config, 'r') as f:
        config = json.load(f)

    keys = config["keys"]

    # Randomly select a key
    key_index = random.randint(0,len(keys)-1)
    print("Using Key: " + str(keys[key_index]["kid"]) + " to sign URI.")
    key = keys[key_index]

    # Build Out claimset
    claimset = {}
    if ("iss" in config.keys()):
        claimset["iss"] = config["iss"]

    if ("token_lifetime" in config.keys()):
        claimset["exp"] = int(time.time()) + config["token_lifetime"]
    else:
        claimset["exp"] = int(time.time()) + 30

    if("aud" in config.keys()):
        claimset["aud"] = config["aud"]

    if("cdnistt"  in config.keys()):
        if config["cdnistt"]:
            claimset["cdnistt"] = 1
            if("cdniets" in config.keys()):
                claimset["cdniets"] = config["cdniets"]
            else:
                claimset["cdniets"] = 30

    Token = jwt.encode(claimset,key,algorithm=key["alg"])

    print("Signed URL: " + args.uri + "?urisigning=" + Token)

if __name__ == "__main__":
     main()
