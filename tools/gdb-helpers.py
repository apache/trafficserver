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
# The History printing works best with `set print array on`.
#

import gdb
import re
import socket
import struct
from curses.ascii import isgraph


class HistoryEntryPrinter:
    """Pretty-printer for a HistoryEntry"""

    def __init__(self, val):
        self.val = val

    @staticmethod
    def event_value_to_description(event_num: int) -> str:
        """Convert event value to a human-readable description.
        :param event_num: The event number.
        :return: A string description of the event.
        """
        if event_num == 60000:
            return 'TS_EVENT_HTTP_CONTINUE'
        elif event_num == 0:
            return 'TS_EVENT_NONE'
        elif event_num == 1:
            return 'TS_EVENT_IMMEDIATE'
        elif event_num == 2:
            return 'TS_EVENT_TIMEOUT'
        elif event_num == 3:
            return 'TS_EVENT_ERROR'
        elif event_num == 4:
            return 'TS_EVENT_CONTINUE'
        elif event_num == 34463:
            return 'NO_EVENT'

        elif event_num == 100:
            return 'TS_EVENT_VCONN_READ_READY'
        elif event_num == 101:
            return 'TS_EVENT_VCONN_WRITE_READY'
        elif event_num == 102:
            return 'TS_EVENT_VCONN_READ_COMPLETE'
        elif event_num == 103:
            return 'TS_EVENT_VCONN_WRITE_COMPLETE'
        elif event_num == 104:
            return 'TS_EVENT_VCONN_EOS'
        elif event_num == 105:
            return 'TS_EVENT_VCONN_INACTIVITY_TIMEOUT'
        elif event_num == 106:
            return 'TS_EVENT_VCONN_ACTIVE_TIMEOUT'
        elif event_num == 107:
            return 'TS_EVENT_VCONN_START'
        elif event_num == 108:
            return 'TS_EVENT_VCONN_CLOSE'
        elif event_num == 109:
            return 'TS_EVENT_VCONN_OUTBOUND_START'
        elif event_num == 110:
            return 'TS_EVENT_VCONN_OUTBOUND_CLOSE'
        elif event_num == 107:
            return 'TS_EVENT_VCONN_PRE_ACCEPT'

        elif event_num == 200:
            return 'TS_EVENT_NET_CONNECT'
        elif event_num == 201:
            return 'TS_EVENT_NET_CONNECT_FAILED'
        elif event_num == 202:
            return 'TS_EVENT_NET_ACCEPT'
        elif event_num == 204:
            return 'TS_EVENT_NET_ACCEPT_FAILED'

        elif event_num == 206:
            return 'TS_EVENT_INTERNAL_206'
        elif event_num == 207:
            return 'TS_EVENT_INTERNAL_207'
        elif event_num == 208:
            return 'TS_EVENT_INTERNAL_208'
        elif event_num == 209:
            return 'TS_EVENT_INTERNAL_209'
        elif event_num == 210:
            return 'TS_EVENT_INTERNAL_210'
        elif event_num == 211:
            return 'TS_EVENT_INTERNAL_211'
        elif event_num == 212:
            return 'TS_EVENT_INTERNAL_212'

        elif event_num == 500:
            return 'TS_EVENT_HOST_LOOKUP'
        elif event_num == 1102:
            return 'TS_EVENT_CACHE_OPEN_READ'
        elif event_num == 1103:
            return 'TS_EVENT_CACHE_OPEN_READ_FAILED'
        elif event_num == 1108:
            return 'TS_EVENT_CACHE_OPEN_WRITE'
        elif event_num == 1109:
            return 'TS_EVENT_CACHE_OPEN_WRITE_FAILED'
        elif event_num == 1112:
            return 'TS_EVENT_CACHE_REMOVE'
        elif event_num == 1113:
            return 'TS_EVENT_CACHE_REMOVE_FAILED'
        elif event_num == 1120:
            return 'TS_EVENT_CACHE_SCAN'
        elif event_num == 1121:
            return 'TS_EVENT_CACHE_SCAN_FAILED'
        elif event_num == 1122:
            return 'TS_EVENT_CACHE_SCAN_OBJECT'
        elif event_num == 1123:
            return 'TS_EVENT_CACHE_SCAN_OPERATION_BLOCKED'
        elif event_num == 1124:
            return 'TS_EVENT_CACHE_SCAN_OPERATION_FAILED'
        elif event_num == 1125:
            return 'TS_EVENT_CACHE_SCAN_DONE'
        elif event_num == 1126:
            return 'TS_EVENT_CACHE_LOOKUP'
        elif event_num == 1127:
            return 'TS_EVENT_CACHE_READ'
        elif event_num == 1128:
            return 'TS_EVENT_CACHE_DELETE'
        elif event_num == 1129:
            return 'TS_EVENT_CACHE_WRITE'
        elif event_num == 1130:
            return 'TS_EVENT_CACHE_WRITE_HEADER'
        elif event_num == 1131:
            return 'TS_EVENT_CACHE_CLOSE'
        elif event_num == 1132:
            return 'TS_EVENT_CACHE_LOOKUP_READY'
        elif event_num == 1133:
            return 'TS_EVENT_CACHE_LOOKUP_COMPLETE'
        elif event_num == 1134:
            return 'TS_EVENT_CACHE_READ_READY'
        elif event_num == 1135:
            return 'TS_EVENT_CACHE_READ_COMPLETE'

        elif event_num == 1200:
            return 'TS_EVENT_INTERNAL_1200'

        elif event_num == 2000:
            return 'TS_EVENT_SSL_SESSION_GET'
        elif event_num == 2001:
            return 'TS_EVENT_SSL_SESSION_NEW'
        elif event_num == 2002:
            return 'TS_EVENT_SSL_SESSION_REMOVE'

        elif event_num == 3900:
            return 'TS_EVENT_AIO_DONE'

        elif event_num == 60000:
            return 'TS_EVENT_HTTP_CONTINUE'
        elif event_num == 60001:
            return 'TS_EVENT_HTTP_ERROR'
        elif event_num == 60002:
            return 'TS_EVENT_HTTP_READ_REQUEST_HDR'
        elif event_num == 60003:
            return 'TS_EVENT_HTTP_OS_DNS'
        elif event_num == 60004:
            return 'TS_EVENT_HTTP_SEND_REQUEST_HDR'
        elif event_num == 60005:
            return 'TS_EVENT_HTTP_READ_CACHE_HDR'
        elif event_num == 60006:
            return 'TS_EVENT_HTTP_READ_RESPONSE_HDR'
        elif event_num == 60007:
            return 'TS_EVENT_HTTP_SEND_RESPONSE_HDR'
        elif event_num == 60008:
            return 'TS_EVENT_HTTP_REQUEST_TRANSFORM'
        elif event_num == 60009:
            return 'TS_EVENT_HTTP_RESPONSE_TRANSFORM'
        elif event_num == 60010:
            return 'TS_EVENT_HTTP_SELECT_ALT'
        elif event_num == 60011:
            return 'TS_EVENT_HTTP_TXN_START'
        elif event_num == 60012:
            return 'TS_EVENT_HTTP_TXN_CLOSE'
        elif event_num == 60013:
            return 'TS_EVENT_HTTP_SSN_START'
        elif event_num == 60014:
            return 'TS_EVENT_HTTP_SSN_CLOSE'
        elif event_num == 60015:
            return 'TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE'
        elif event_num == 60016:
            return 'TS_EVENT_HTTP_PRE_REMAP'
        elif event_num == 60017:
            return 'TS_EVENT_HTTP_POST_REMAP'
        elif event_num == 60018:
            return 'TS_EVENT_HTTP_REQUEST_BUFFER_COMPLETE'

        elif event_num == 60100:
            return 'TS_EVENT_LIFECYCLE_PORTS_INITIALIZED'
        elif event_num == 60101:
            return 'TS_EVENT_LIFECYCLE_PORTS_READY'
        elif event_num == 60102:
            return 'TS_EVENT_LIFECYCLE_CACHE_READY'
        elif event_num == 60103:
            return 'TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED'
        elif event_num == 60104:
            return 'TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED'
        elif event_num == 60105:
            return 'TS_EVENT_LIFECYCLE_MSG'
        elif event_num == 60106:
            return 'TS_EVENT_LIFECYCLE_TASK_THREADS_READY'
        elif event_num == 60107:
            return 'TS_EVENT_LIFECYCLE_SHUTDOWN'

        elif event_num == 60200:
            return 'TS_EVENT_INTERNAL_60200'
        elif event_num == 60201:
            return 'TS_EVENT_INTERNAL_60201'
        elif event_num == 60202:
            return 'TS_EVENT_INTERNAL_60202'
        elif event_num == 60203:
            return 'TS_EVENT_SSL_CERT'
        elif event_num == 60204:
            return 'TS_EVENT_SSL_SERVERNAME'
        elif event_num == 60205:
            return 'TS_EVENT_SSL_VERIFY_SERVER'
        elif event_num == 60206:
            return 'TS_EVENT_SSL_VERIFY_CLIENT'
        elif event_num == 60207:
            return 'TS_EVENT_SSL_CLIENT_HELLO'
        elif event_num == 60208:
            return 'TS_EVENT_SSL_SECRET'

        elif event_num == 60300:
            return 'TS_EVENT_MGMT_UPDATE'

        else:
            return f'Unknown event {event_num}'

    def to_string(self):
        loc = self.val['location']

        file = loc['file'].string()
        line = int(loc['line'])
        base_name = file.split('/')[-1]
        location_description = f'{base_name}:{line}'

        function = loc['func'].string() if loc['func'] else ''

        event = int(self.val['event'])
        event_name = self.event_value_to_description(event)
        event_description = f'{event_name} ({event})'

        reent = int(self.val['reentrancy'])
        return f"{location_description:<25} {function:<50} {event_description:<55} reent={reent:>7}"


class HistoryPrinter:
    '''Pretty-printer for History<Count>.

    This is in the HttpSM class as 'history', for example.
    '''

    def __init__(self, val):
        self.val = val
        t = val.type.strip_typedefs().unqualified()
        name = t.name or ''
        m = re.match(r'.*History<([0-9]+)>', name)
        self.capacity = int(m.group(1)) if m else None
        self.history_position = int(val['history_pos'])

    def to_string(self):
        num_entries = self.capacity if self.history_position >= self.capacity else self.history_position
        return f"History(capacity={self.capacity}, history_pos={self.history_position}, num_entries={num_entries})"

    def children(self):
        '''Print the history entries from earliest to latest.

        This handles the circular buffer nature of the history.
        '''
        has_wrapped = self.history_position >= self.capacity
        first_entry = self.history_position % self.capacity if has_wrapped else 0
        num_entries = self.capacity if has_wrapped else self.history_position

        history_entries = self.val['history']
        for i in range(num_entries):
            index = (first_entry + i) % self.capacity
            history_entry = history_entries[index]
            location = history_entry['location']
            func = location['func'].string()
            if 'state_api_call' in func or 'set_next_state' in func:
                # These are generally not useful.
                continue
            yield f"[{i}]", history_entry

    # Set display_hint.
    def display_hint(self):
        '''Indicate that History is an array-like object.'''
        return 'array'


def lookup_history_printer(val):
    t = val.type.strip_typedefs().unqualified()
    name = t.name or ''
    # Handle plain HistoryEntry
    if name == 'HistoryEntry':
        return HistoryEntryPrinter(val)
    # Handle History<Count> template
    if re.match(r'.*History<\d+>', name):
        return HistoryPrinter(val)
    return None


# Register our pretty-printers.
if hasattr(gdb, 'pretty_printers'):
    gdb.pretty_printers.append(lookup_history_printer)


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
