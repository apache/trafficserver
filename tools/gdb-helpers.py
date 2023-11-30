#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
##
# http://www.apache.org/licenses/LICENSE-2.0
##
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# This script will add a command to help print out ATS data structures in gdb.
#
# In a .gdbinit file or from the gdb command prompt:
#   source <path to this file>.py
#
# Then you can type:  `atspr sm this`  Where this is a HttpSM* and it will print some information about it.
#
#  For the expression you should be able to put anything that can be cast to the appropriate type.
#
# Type `atspr` by itself for a list of printables.
#

import gdb
import socket
import struct
from curses.ascii import isgraph

# return memory for an ats string address and length


def ats_str(addr, addr_len):
    if addr_len == 0:
        return ''
    #print("addr {} len {}".format(addr, addr_len))
    inferior = gdb.selected_inferior()
    try:
        buff = inferior.read_memory(addr, addr_len)
        if buff[0] == '\x00':
            return 'null'
        else:
            return buff
    except BaseException:
        return 'unreadable({:x})'.format(int(addr))


def hdrtoken(idx):
    hdrtokens = gdb.parse_and_eval('hdrtoken_strs')
    return hdrtokens[idx].string()


def wks_or_str(idx, addr, addr_len):
    if idx >= 0:
        return hdrtoken(idx)
    else:
        return ats_str(addr, addr_len)


class URL:

    def __init__(self, val):
        self.impl = val['m_url_impl'].dereference()

    def __str__(self):
        return "{}://{}/{}".format(self.scheme(), self.host(), self.path())

    def scheme(self):
        return ats_str(self.impl['m_ptr_scheme'], self.impl['m_len_scheme'])

    def host(self):
        return ats_str(self.impl['m_ptr_host'], self.impl['m_len_host'])

    def path(self):
        return ats_str(self.impl['m_ptr_path'], self.impl['m_len_path'])


class HTTPHdr:

    def __init__(self, val):
        self.impl = val['m_http']
        self.val = val

    def url(self):
        return URL(self.val['m_url_cached'])

    def is_request(self):
        pol = self.impl['m_polarity']
        return pol == 1

    def is_response(self):
        pol = self.impl['m_polarity']
        return pol == 2

    def is_valid(self):
        if self.impl == 0:
            return None
        pol = self.impl['m_polarity']
        return pol > 0

    def method(self):
        idx = self.impl['u']['req']['m_method_wks_idx']
        if idx >= 0:
            return hdrtoken(idx)
        else:
            return ats_str(self.impl['u']['req']['m_ptr_method'], self.impl['u']['req']['m_len_method'])

    def status(self):
        return self.impl['u']['resp']['m_status']

    def headers(self):
        mime = self.val['m_mime'].dereference()
        fblock_ptr = mime['m_first_fblock'].address
        while fblock_ptr != 0:
            fblock = fblock_ptr.dereference()
            slots = fblock['m_field_slots']
            #print("slots type {} address {} size {}".format(slots.type, slots.address, slots.type.sizeof))
            #print("next idx {} len {} next {}".format(fblock['m_freetop'], fblock['m_length'], fblock['m_next']))

            for slot_idx in range(fblock['m_freetop']):
                fld = slots[slot_idx]
                wks = fld['m_wks_idx']
                name = wks_or_str(wks, fld['m_ptr_name'], fld['m_len_name'])
                yield (name, ats_str(fld['m_ptr_value'], fld['m_len_value']))

            fblock_ptr = fblock['m_next']

    def pr(self):
        if self.is_valid():
            if self.is_request():
                print("{} {}".format(self.method(), self.url()))
            if self.is_response():
                print("status: {}".format(self.status()))
            for key, val in self.headers():
                print("{}: {}".format(key, val))
        else:
            print("invalid")


class ConnectionAttributes:

    def __init__(self, val):
        self.val = val

    def http_version(self):
        v = self.val['http_version']['m_version']
        return (v >> 16, v & 0xffff)

    def src_addr(self):
        ip = self.val['src_addr']
        s = struct.pack('!I', socket.ntohl(int(ip['sin']['sin_addr']['s_addr'])))
        return socket.inet_ntoa(s)

    def dst_addr(self):
        ip = self.val['dst_addr']
        s = struct.pack('!I', socket.ntohl(int(ip['sin']['sin_addr']['s_addr'])))
        return socket.inet_ntoa(s)


class HttpSM:

    def __init__(self, val):
        ptr_type = gdb.lookup_type('struct HttpSM').pointer()
        self.val = None

        if val.type != ptr_type:
            self.val = val.cast(ptr_type)
        else:
            self.val = val

    def id(self):
        return self.val['sm_id']

    def client_request(self):
        return HTTPHdr(self.val['t_state']['hdr_info']['client_request'])

    def client_response(self):
        return HTTPHdr(self.val['t_state']['hdr_info']['client_response'])

    def server_request(self):
        return HTTPHdr(self.val['t_state']['hdr_info']['server_request'])

    def server_response(self):
        return HTTPHdr(self.val['t_state']['hdr_info']['server_response'])

    def transform_response(self):
        return HTTPHdr(self.val['t_state']['hdr_info']['transform_response'])

    def cache_response(self):
        return HTTPHdr(self.val['t_state']['hdr_info']['cache_response'])

    def client_info(self):
        return ConnectionAttributes(self.val['t_state']['client_info'])

    def parent_info(self):
        return ConnectionAttributes(self.val['t_state']['client_info'])

    def server_info(self):
        return ConnectionAttributes(self.val['t_state']['client_info'])


def sm_command(val):
    sm = HttpSM(val)

    print('smid = %s' % sm.id())
    print('---- request ----')
    sm.client_request().pr()
    print('---- response ----')
    sm.client_response().pr()
    print('---- server_request  ----')
    sm.server_request().pr()
    print('---- server_response  ----')
    sm.server_response().pr()
    print('---- transform_response  ----')
    sm.transform_response().pr()
    print('---- cache_response  ----')
    sm.cache_response().pr()
    print('---- client_info  ----')
    info = sm.client_info()
    major, minor = info.http_version()
    print("HTTP {}.{} ({} -> {})".format(major, minor, info.src_addr(), info.dst_addr()))
    print('---- server_info  ----')
    info = sm.server_info()
    major, minor = info.http_version()
    print("HTTP {}.{} ({} -> {})".format(major, minor, info.src_addr(), info.dst_addr()))
    print('---- parent_info  ----')
    info = sm.parent_info()
    major, minor = info.http_version()
    print("HTTP {}.{} ({} -> {})".format(major, minor, info.src_addr(), info.dst_addr()))


def hdrs_command(val):
    HTTPHdr(val).pr()


def url_command(val):
    print(URL(val))


commands = [
    ("sm", sm_command, "Print HttpSM details (type HttpSM*)"),
    ("hdrs", hdrs_command, "Print HttpHdr details (type HTTPHdr)"),
    ("url", url_command, "Print URL"),
]


def usage():
    print("Usage: atspr <command> <expr>")
    print("commands:")
    for cmd, f, desc in commands:
        print("  {}: {}".format(cmd, desc))


class ATSPrintCommand(gdb.Command):

    def __init__(self):
        super(ATSPrintCommand, self).__init__('atspr', gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        if len(argv) < 2:
            usage()
            return

        what = argv[0]
        expr = argv[1]
        val = gdb.parse_and_eval(expr)

        for cmd, f, _ in commands:
            if cmd == what:
                f(val)
                return

        usage()


ATSPrintCommand()

# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
