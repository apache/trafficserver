#!/usr/bin/env python
# this script sets up the testing packages to allow the tests

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

from __future__ import absolute_import, division, print_function

import argparse
import os
import subprocess
import platform
import sys

pip_packages = [
    "autest==1.8.0",
    "hyper",
    "requests",
    "dnslib",
    "httpbin",
    "gunicorn",
    "traffic-replay"  # this should install TRLib, MicroServer, MicroDNS, Traffic-Replay
]


distro_packages = {
    "RHEL": [
        "install epel-release",
        "install python36",
        "install rh-python36-python-virtualenv"
    ],
    "Fedora": [
        "install python3",
        "install python3-virtualenv",
        "install python-virtualenv",
    ],
    "Ubuntu": [
        "install python3",
        "install python3-virtualenv",
        "install virtualenv",
        "install python3-dev"
    ],
    "CentOS": [
        "install epel-release",
        "install rh-python36-python-virtualenv"
    ],
    "CentOS-8": [
        "install epel-release",
        "install python3-virtualenv"
    ]
}


def command_output(cmd_str):
    print(cmd_str)
    proc = subprocess.Popen(
        cmd_str,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True)

    # while command runs get output
    while proc.poll() == None:
        tmp = proc.stdout.readline()
        sys.stdout.write(tmp)

    for last_output in proc.stdout.readlines():
        sys.stdout.write(last_output)

    return proc.returncode


def get_distro():
    return platform.linux_distribution()


def distro_version():
    return int(get_distro()[1].split(".")[0])


def isFedora():
    return get_distro()[0].startswith("Fedora")


def isCentOS():
    return get_distro()[0].startswith("CentOS")


def distro():
    if isFedora():
        return "Fedora"
    if isCentOS():
        return "CentOS"
    if get_distro()[0].startswith("Red Hat"):
        return "RHEL"
    if get_distro()[0].startswith("Ubuntu"):
        return "Ubuntu"


def isRedHatBased():
    return get_distro()[0].startswith("Red Hat") or get_distro()[0].startswith(
        "Fedora") or get_distro()[0].startswith("CentOS")


def isInstalled(prog):
    out = subprocess.Popen(
        ["which", prog], stdout=subprocess.PIPE).communicate()
    if out[0] != '':
        return True
    return False


def installManagerName():
    if isRedHatBased() and distro_version() >= 22:
        ret = "sudo dnf -y"  # Fedora 22 or newer
    elif isRedHatBased():
        ret = "sudo yum -y"  # Red Hat distro
    else:
        ret = "sudo apt-get -y"  # Ubuntu/Debian

    return ret


def installToolName():
    if isRedHatBased():
        ret = "rpm -ihv"  # Red Hat Based
    else:
        ret = "dpkg -iv"  # Ubuntu/Debian

    return ret


def run_cmds(cmds):
    for cmd in cmds:
        # print (cmd.split[" "])
        # subprocess.call(cmd.split[" "])
        if command_output(cmd):
            print("'{0}'' - Failed".format(cmd))


def gen_package_cmds(packages):

    # main install tool/manager (yum, dnf, apt-get, etc)
    mtool = installManagerName()
    # core install tool (rpm, dpkg, etc)
    itool = installToolName()
    ret = []

    for p in packages:
        if p.startswith("wget"):
            pth = p[5:]
            pack = os.path.split(pth)[1]
            cmd = ["wget {0}".format(pth), "{0} ./{1}".format(itool, pack)]
        else:
            cmd = ["{0} {1}".format(mtool, p)]
        ret.extend(cmd)
    return ret


extra = ''
if distro() == 'RHEL' or (distro() == 'CentOS' and distro_version() < 8):
    extra = ". /opt/rh/rh-python36/enable ;"


def venv_cmds(path):
    '''
    Create virtual environment and add it
    to the path being used for the script
    '''

    return [
        # first command only needed for rhel and centos systems at this time
        extra + " virtualenv --python=python3 {0}".format(path),
        extra + " {0}/bin/pip install pip setuptools --upgrade".format(path)
    ]


def main():
    " main script logic"
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--use-pip", nargs='?', default="pip", help="Which pip to use")

    parser.add_argument(
        "venv_path",
        nargs='?',
        default="env-test",
        help="The directory to us to for the virtualenv")

    parser.add_argument(
        "--disable-virtualenv",
        default=False,
        action='store_true',
        help="Do not create virtual environment to install packages under")

    parser.add_argument(
        '-V', '--version', action='version', version='%(prog)s 1.0.0')

    args = parser.parse_args()
    # print(args)
    # print(get_distro())

    # do we know of packages to install for the given platform
    dist = distro()
    cmds = []

    # if centos 8 we must set crypto to legacy to allow tlsv1.0 tests
    if dist:
        if distro() == 'CentOS' and distro_version() > 7:
            cmds += ["sudo update-crypto-policies --set LEGACY"]

    if dist:
        if distro() == 'CentOS' and distro_version() > 7:
            cmds += gen_package_cmds(distro_packages['CentOS-8'])
        else:
            cmds += gen_package_cmds(distro_packages[dist])

    # test to see if we should use a certain version of pip
    path_to_pip = None
    if args.use_pip != "pip":
        path_to_pip = args.use_pip

    # install on the system, or use virtualenv for pip based stuff
    if not args.disable_virtualenv:
        # Create virtual env
        cmds += venv_cmds(args.venv_path)
        if path_to_pip is None:
            path_to_pip = os.path.join(args.venv_path, "bin", args.use_pip)

    cmds += [extra + "{0} install {1}".format(path_to_pip, " ".join(pip_packages))]

    run_cmds(cmds)


if __name__ == '__main__':
    main()
