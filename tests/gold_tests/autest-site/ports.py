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

import socket
import subprocess
import os
import platform

import hosts.output as host

from ordered_set_queue import OrderedSetQueue


g_ports = None  # ports we can use


def PortOpen(port, address=None):

    ret = False
    if address is None:
        address = "localhost"

    address = (address, port)

    try:
        s = socket.create_connection(address, timeout=.5)
        s.close()
        ret = True
    except socket.error:
        s = None
        ret = False
    except socket.timeout:
        s = None

    return ret


def setup_port_queue(amount=1000):
    global g_ports
    if g_ports is None:
        g_ports = OrderedSetQueue()
    else:
        return
    try:
        # some docker setups don't have sbin setup correctly
        new_env = os.environ.copy()
        new_env['PATH'] = "/sbin:/usr/sbin:" + new_env['PATH']
        if 'Darwin' == platform.system():
            dmin = subprocess.check_output(
                ["sysctl", "net.inet.ip.portrange.first"],
                env=new_env
            ).decode().split(":")[1].split()[0]
            dmax = subprocess.check_output(
                ["sysctl", "net.inet.ip.portrange.last"],
                env=new_env
            ).decode().split(":")[1].split()[0]
        else:
            dmin, dmax = subprocess.check_output(
                ["sysctl", "net.ipv4.ip_local_port_range"],
                env=new_env
            ).decode().split("=")[1].split()
        dmin = int(dmin)
        dmax = int(dmax)
    except:
        host.WriteWarning("Unable to call sysctrl!\n Tests may fail because of bad port selection!")
        return

    rmin = dmin - 2000
    rmax = 65536 - dmax

    if rmax > amount:
        # fill in ports
        port = dmax + 1
        while port < 65536 and g_ports.qsize() < amount:
            # if port good:
            if not PortOpen(port):
                g_ports.put(port)
            port += 1
    if rmin > amount and g_ports.qsize() < amount:
        port = 2001
        while port < dmin and g_ports.qsize() < amount:
            # if port good:
            if not PortOpen(port):
                g_ports.put(port)
            port += 1


def get_port(obj, name):
    '''
    Get a port and set it to a variable on the object

    '''

    setup_port_queue()
    if g_ports.qsize():
        # get port
        port = g_ports.get()
        # assign to variable
        obj.Variables[name] = port
        # setup clean up step to recycle the port
        obj.Setup.Lambda(func_cleanup=lambda: g_ports.put(
            port), description="recycling port")
        return port

    # use old code
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', 0))  # bind to all interfaces on an ephemeral port
    port = sock.getsockname()[1]
    obj.Variables[name] = port
    return port
