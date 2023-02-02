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
import traceback
import yaml
import tempfile
import fileinput

import io

from io import StringIO
from string import Template
from pydoc import locate
from jsonschema import validate
from jsonschema.exceptions import ValidationError
from colorama import init, Fore, Back, Style
init(autoreset=True)

Remapped_Vars = {}

Renamed_Records = {
    # as it can have a numeric value [0-2]
    'proxy.config.output.logfile': 'proxy.config.output.logfile.name',
    'proxy.config.exec_thread.autoconfig': 'proxy.config.exec_thread.autoconfig.enabled',
    'proxy.config.hostdb': 'proxy.config.hostdb.enabled',
    'proxy.config.tunnel.prewarm': 'proxy.config.tunnel.prewarm.enabled',
    'proxy.config.ssl.origin_session_cache': 'proxy.config.ssl.origin_session_cache.enabled',
    'proxy.config.ssl.session_cache': 'proxy.config.ssl.session_cache.value',
    'proxy.config.ssl.TLSv1_3': 'proxy.config.ssl.TLSv1_3.enabled',
    'proxy.config.ssl.client.TLSv1_3': 'proxy.config.ssl.client.TLSv1_3.enabled'
}

###############################################################################################
###############################################################################################


def red(text, bright=False):
    return f"{Style.BRIGHT if bright else ''}{Fore.RED}{text}{Style.RESET_ALL}"


def bright(text, color=Fore.GREEN):
    return f'{Style.BRIGHT}{color}{text}{Style.RESET_ALL}'


def error():
    return f"{red('[E] ', True)}"


def fancy_chain_print(lst, isError=False):
    output = StringIO()
    if len(lst) == 1:
        output.write(f'» {lst[0]}\n')
        s = output.getvalue()
        output.close()
        return s
    space = ''
    output.write(f'┌■ {lst[0]}\n')
    if len(lst) > 2:
        space = ' '
        # from 2nd till end-1
        output.write(f'└┬──» {lst[1]}\n')
        for x in lst[2:-1]:
            output.write(f' ├──» {x}\n')

    z = lst[-1]
    output.write(f'{space}└──» {z}\n')

    s = output.getvalue()
    output.close()
    return s


def progressbar(it, size=60, out=sys.stdout, mute=False):
    count = len(it)

    def show(j):
        x = int(size * j / count)
        if not mute:
            print("[{}{}] {}/{}".format("█" * x, "░" * (size - x), j, count),
                  end='\r', file=out, flush=True)
    show(0)
    for i, item in enumerate(it):
        yield item

        show(i + 1)
    if not mute:
        print("\n", flush=True, file=out)


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


def save_to_file(filename, is_json, typerepr, data):

    def float_representer(dumper, value):
        return dumper.represent_scalar(u'tag:yaml.org,2002:float', str(value), style="'")

    def int_representer(dumper, value):
        return dumper.represent_scalar(u'tag:yaml.org,2002:int', str(value), style="'")

    def bool_representer(dumper, value):
        return dumper.represent_scalar(u'tag:yaml.org,2002:bool', str(value), style="'")

    def null_representer(dumper, value):
        return dumper.represent_scalar(u'tag:yaml.org,2002:null', str(value), style="'")

    with open(filename, 'w') as f:
        if is_json:
            json.dump(data, f, indent=4, sort_keys=True)
        else:
            if 'float' in typerepr:
                yaml.add_representer(float, float_representer)
            if 'int' in typerepr:
                yaml.add_representer(int, int_representer)
            if 'bool' in typerepr:
                yaml.add_representer(bool, bool_representer)
            if 'null' in typerepr:
                yaml.add_representer(None, null_representer)
            yaml.dump(data, f)


def print_summary():
    if len(Remapped_Vars):
        out = []
        out.append(f'{Style.BRIGHT}{Fore.GREEN}{len(Remapped_Vars)} Renamed records:{Style.RESET_ALL}')
        idx = 1
        for k, v in Remapped_Vars.items():
            out.append(f'{Style.BRIGHT}{Fore.GREEN}#{idx}{Style.RESET_ALL} : {k} -> {Style.BRIGHT}{Fore.GREEN}{v}{Style.RESET_ALL}')
            idx = idx + 1

        print(fancy_chain_print(out))


def get_value(type, value):
    def have_multipliers(value, mps):
        for m in mps:
            if value.endswith(m):
                return True

        return False

    if value == 'nullptr' or value == 'NULL':
        return None

    # We want to make the type right when inserting the element into the dict.
    if type == 'FLOAT':
        return float(value)
    elif type == 'INT':
        # We need to make it YAML compliant as this will be an int, so if contains
        # any special character like hex or a multiplier, then we make a string. ATS will
        # parse it as string anyway.
        if value.startswith('0x') or have_multipliers(value, ['K', 'M', 'G', 'T']):
            return str(value)
        else:
            return int(value)
    elif type == 'STRING':
        return str(value)

    return None


def add_object(config, var, value, type=None):
    key = ''
    index = var.find('.')
    if index < 0:  # last part
        config[var] = get_value(type, value)
    else:
        key = var[:index]
        if key not in config:
            config[key] = {}

        add_object(config[key], var[index + 1:], value, type=type)


def fix_record_names(file):
    temp = tempfile.NamedTemporaryFile(prefix='records.config_', delete=False)

    for l in fileinput.input(files=file):
        elements = l.split()
        if len(elements) == 4:
            rec_name = elements[1]
            if rec_name in Renamed_Records:
                elements[1] = Renamed_Records[rec_name]

                rec_line = ' '.join(elements)
                temp.write(rec_line.encode('utf-8'))
                temp.write(b'\n')
                Remapped_Vars[rec_name] = Renamed_Records[rec_name]

                continue
        # if elements len diff from 4 .. well.. it shouldn't.
        temp.write(l.encode('utf-8'))

    temp.close()
    return temp.name


def handle_file_input(args):
    if args.file is None:
        raise Exception("Hey!! there is no file!")

    config = {}

    # Fix the names first.
    filename = fix_record_names(args.file)
    with open(filename, "r+") as f:
        lines = f.readlines()
        num_lines = len(lines)
        for idx in progressbar(range(num_lines), 40, mute=args.mute):
            line = lines[idx]
            if '#' == line[0] or '\n' == line[0] or ' ' == line[0]:
                idx = idx + 1
                continue
            s = line.split(' ', maxsplit=3)  # keep values all together
            name = s[1]
            type = s[2]
            value = s[3]

            # We ignore the prefix and work away from  there.
            if name.startswith("proxy.config."):
                name = name[len("proxy.config."):]
            elif name.startswith("local.config."):
                name = name[len("local.config."):]
            elif args.node and name.startswith("proxy.node."):
                name = name[len("proxy."):]

            # Build the object
            add_object(config, name, value[:-1], type)
            idx = idx + 1

    ts = {}
    ts['ts'] = config

    if args.schema:
        err = validate_schema(ts, args.schema)
        if err:
            raise Exception("Schema failed.")

    save_to_file(args.save2, args.json, args.typerepr, ts)

    f.close()
    return


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='records.config to YAML/JSON convert tool')
    parser.add_argument('-f', '--file', help='records.config input file.')
    parser.add_argument('-n', '--node', help="Include 'proxy.node' variables in the parser.", action='store_true')
    parser.add_argument('-t', '--typerepr', help="Use type representer (list)", required=False, default=[''])
    parser.add_argument('-s', '--schema', help="Validate the output using a json schema file.")
    parser.add_argument('-S', '--save2', help="Save to file.", required=True)
    parser.add_argument(
        '-m',
        '--mute',
        help="Be quiet, do not output anything, except for errors",
        required=False,
        action='store_true')
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
        if args.mute is False:
            print_summary()

    except Exception as e:
        print("Something went wrong: {}".format(e))

        print(traceback.format_exc())
        sys.exit(1)

    sys.exit(0)
