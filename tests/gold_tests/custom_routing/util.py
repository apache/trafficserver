from pprint import pprint
import os
import socket
from ports import get_port
import socket
import time

When = None
Test = None
Testers = None
localhost = socket.gethostname()
start_time = time.clock()


def pluginPath():
    p = Test.TestDirectory
    return p[:p.rfind('/')]


def getStr(obj):
    try:
        return obj.Name
    except BaseException:
        pass
    return str(obj)


def debugOut(func, *kargs):
    dt = str(int(1000 * (time.clock() - start_time)))
    print('{:5} {:<15} -'.format(dt, func), *kargs)


'''
'''
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


def extract(content, prefix, suffix):
    a = content.find(prefix)
    if a == -1:
        return None
    a += len(prefix)
    b = content.find(suffix, a)
    if b == -1:
        return None
    return content[a:b]


def addLogging(ts, filename, name, log_fmt_str):

    ts.Disk.records_config.update({
        'proxy.config.log.logging_enabled': 3,
        'proxy.config.log.max_secs_per_buffer': 1,
    })

    if hasattr(ts.Disk, 'logging_yaml'):
        cfg_text = '''
logging:
  formats:
    - name: {name}
      format: "{fmt}"
  logs:
    - filename: {filename}
      format: {name}
'''
        cfg_file = ts.Disk.logging_yaml
    else:
        cfg_text = '''{name} = format {
        Format = "{fmt}"
        }

        log.ascii {
        Format = {name},
        Filename = '{filename}'
        } '''
        cfg_file = ts.Disk.logging_config

    cfg_text = cfg_text.format(filename=filename, fmt=log_fmt_str, name=name)
    cfg_file.AddLine(cfg_text)
    debugOut('addLogging', filename)

    return os.path.join(ts.Variables.LOGDIR, filename)


def saveLogging(ts, filename):
    tr = Test.AddTestRun('Save ' + filename)
    tr.Processes.Default.Command = '; sleep 1'
    ts_list = ts if isinstance(ts, list) else[ts]
    for ts in ts_list:
        src_f = ts.Disk.File(filename)
        dst_f = tr.Disk.File(ts.Name + '_' + filename)
        debugOut('move file ', [src_f, dst_f])
        tr.Processes.Default.Command += '; mv {src} {dst}'.format(src_f.filename, dst_f.filename)


def logFindStrings(content, substr_list):
    debugOut('logFindStrings', substr_list)
    for line in content.split('\n'):
        i = 0
        found = ''
        for s in substr_list:
            i = line.find(s, i)
            if i == -1:
                break
            found += '^' * (i - len(found)) + s
            i += len(s)

        if i > 0:
            print('\n' + line)
            print(found)
            return ''  # everything found in one line
    print('missing ' + str(substr_list))
    return 'missing ' + str(substr_list)


def logContains(log, substr_list):
    def f(content): return logFindStrings(content, substr_list)
    log += Testers.FileContentCallback(f, 'logFindStrings')

    #g = lambda x: validate_forwards_logged(x, owner_name, ingress)
    #logfilename = os.path.join(ts.Variables.LOGDIR, 'carp{}.log'.format(ts.Variables.port))
    #Test.Disk.File(logfilename, exists=True).Content = Testers.FileContentCallback(g, 'validate_forwards_logged')


def objSearch(obj, key, prefix=[], ignore=None):
    if obj is not dict:
        try:
            methods = dir(obj)
            obj = obj.__dict__
        except BaseException:
            return
    for k in obj:
        if k.find(key) >= 0:
            print('.'.join(prefix + [k]))
            # pprint(type(obj[k]))
            # if type(obj[k]) == dict:
            #    pprint(obj[k])
            # if type(obj[k]) != str:
            #    pprint(dir(obj[k]))
            #    if '__dict__' in dir(obj[k]):
            #        pprint(obj[k].__dict__)
            return
        if ignore and k.find(ignore) >= 0:
            continue
        if len(prefix) < 12 and k not in prefix:
            objSearch(obj[k], key, prefix + [k], ignore)

    for k in methods:
        if k.find(key) >= 0:
            print('.'.join(prefix + [k]))
