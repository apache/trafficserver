#!/usr/bin/env python3
#
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

import argparse
import sys
import json
import ruamel.yaml
import io
from string import Template
from pydoc import locate
from jsonschema import validate
from jsonschema.exceptions import ValidationError


def validate_schema(data, schema_filename):
    if schema_filename:
        with open(schema_filename, 'r') as f:
            s = f.read()
            sdata = json.loads(s)
    try:
        # validate will throw if invalid schema.
        if schema_filename:
            validate(instance=data, schema=sdata)
    except ValidationError as ve:
        return str(ve)

    return ""


def get_mapped_type_str(type, value):
    if type == 'INT':
        # Basically, we do this to wrap the hex values. YAML have no way to express
        # this as hex value as it will translate it to a decimals. JSON in the other
        # hand can, but for now we wrap it into a string.
        try:
            int(value)
        except ValueError:
            return 'str'
        return 'int'
    elif type == 'STRING':
        return 'str'
    elif type == 'FLOAT':
        return 'float'


def get_value(type, value):
    if value == 'nullptr' or value == 'NULL':
        return None
    v = locate(get_mapped_type_str(type, value))

    return v(value)


def add_object(config, var, value, type=None, force_key=None):
    '''
    walk the key and build a map style.
    '''
    obj = {}
    key = ''
    index = var.find('.')
    if index < 0:
        '''
        For cases like this:
          path.to.key INT 1
          path.to.key.subkey INT 2
        we change the approach and add a 'value' field into the key and make
        the main key a structure. So the above example will look like this:
          key: {
            value: 1,
            subkey: 2
          }
        '''
        if force_key:
            obj = {}
            obj['value'] = config[force_key]
            obj[var] = get_value(type, value)
            config[force_key] = obj
        else:
            config[var] = get_value(type, value)
    else:
        key = var[:index]
        if key not in config:
            config[key] = {}
        if not isinstance(config[key], dict):
            add_object(config, var[index + 1:], value, type=type, force_key=key)
        else:
            add_object(config[key], var[index + 1:], value, type=type)


def save_to_file(filename, is_json, data):
    with open(filename, 'w') as f:
        if is_json:
            json.dump(data, f, indent=4, sort_keys=True)
        else:
            ruamel.yaml.round_trip_dump(data, f, explicit_start=True)


def handle_file_input(args):
    if args.file is None:
        raise Exception("Oops, where is the file?")

    config = {}
    filename = args.file
    with open(filename, "r") as f:
        for line in f:
            if '#' == line[0] or '\n' == line[0] or ' ' == line[0]:
                continue
            s = line.split(' ', maxsplit=3)  # keep values all together
            name = s[1]
            type = s[2]
            value = s[3]
            if name.startswith("proxy.config."):
                name = name[len("proxy.config."):]
            elif name.startswith("local.config."):
                name = name[len("local.config."):]
            elif args.node and name.startswith("proxy.node."):
                name = name[len("proxy."):]

            add_object(config, name, value[:-1], type)
    ts = {}
    ts['ts'] = config

    if args.schema:
        err = validate_schema(ts, args.schema)
        if err:
            raise Exception("Schema failed.")

    save_to_file(args.save2, args.json, ts)

    f.close()
    return


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='records.config to YAML/JSON convert tool')
    parser.add_argument('-f', '--file', help='Parse a file and generate a yaml/json output')
    parser.add_argument('-n', '--node', help="Include 'proxy.node' variables in the parser", action='store_true')
    parser.add_argument('-s', '--schema', help="Validate the output using a json schema file")
    parser.add_argument('-S', '--save2', help="Save to file", required=True)
    kk = parser.add_mutually_exclusive_group(required=True)
    kk.add_argument('-j', '--json', help="Output as json", action='store_true')
    kk.add_argument('-y', '--yaml', help="Output as yaml", action='store_true')
    parser.set_defaults(func=handle_file_input)
    args = parser.parse_args()

    try:
        func = args.func
    except AttributeError:
        parser.error("Too few arguments")
    try:
        func(args)
    except Exception as e:
        print("Something went wrong: {}".format(e))
        sys.exit(1)

    sys.exit(0)
