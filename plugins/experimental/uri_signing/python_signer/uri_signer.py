#!/usr/bin/env python3

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
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

  # helpers
  parser.add_argument('--key_index', type=int, nargs=1)
  parser.add_argument('--token_lifetime', type=int, nargs=1)

  # override arguments -- claims
  parser.add_argument('--aud', nargs=1)
  parser.add_argument('--cdniets', type=int, nargs=1)
  parser.add_argument('--cdnistd', type=int, nargs=1)
  parser.add_argument('--cdnistt', type=int, nargs=1)
  parser.add_argument('--exp', type=int, nargs=1)
  parser.add_argument('--iss', nargs=1)

  # override arguments -- key
  parser.add_argument('--alg', nargs=1)
  parser.add_argument('--k', nargs=1)
  parser.add_argument('--kid', nargs=1)
  parser.add_argument('--kty', nargs=1)

  args = parser.parse_args()

  with open(args.config, 'r') as f:
    config = json.load(f)

  keys = config["keys"]

  # Select a key, either explicitly or randomly
  key_index = 0
  if args.key_index:
    key_index = args.key_index[0]
    print("args key_index " + str(key_index))
  else:
    key_index = random.randint(0,len(keys)-1)
    print("randomizing key index")

  print("Using key_index " + str(key_index))

  print("Using Key: " + str(keys[key_index]["kid"]) + " to sign URI.")
  key = keys[key_index]

  # Build Out claimset
  claimset = {}
  if "iss" in config.keys():
    claimset["iss"] = config["iss"]

  if "token_lifetime" in config.keys():
    claimset["exp"] = int(time.time()) + config["token_lifetime"]
  else:
    claimset["exp"] = int(time.time()) + 30

  if "aud" in config.keys():
    claimset["aud"] = config["aud"]

  if "cdnistt" in config.keys():
    if config["cdnistt"]:
      claimset["cdnistt"] = 1
      if "cdniets" in config.keys():
        claimset["cdniets"] = config["cdniets"]
      else:
        claimset["cdniets"] = 30


  # process override args - simple
  if args.iss:
    claimset["iss"] = args.iss[0]
  if args.exp:
    claimset["exp"] = args.exp[0]
  if args.aud:
    claimset["aud"] = args.aud[0]

  # process override args - complex
  if args.cdnistt:
    claimset["cdnistt"] = args.cdnistt[0]

  if "cdnistt" in config.keys():
    if args.cdniets:
      claimset["cdniets"] = arg.cdniets[0]

  # specific key overrides
  if args.alg:
    key["alg"] = args.alg[0]
  if args.kid:
    key["kid"] = args.kid[0]
  if args.kty:
    key["kty"] = args.kty[0]
  if args.k:
    key["k"] = args.k[0]

  print(claimset)
  print(key)

  Token = jwt.encode(claimset,key,algorithm=key["alg"])

  print("Signed URL: " + args.uri + "?URISigningPackage=" + Token)

if __name__ == "__main__":
   main()
