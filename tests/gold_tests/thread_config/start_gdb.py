#!/usr/bin/env python3

import subprocess
import sys

lines = subprocess.check_output('ps -axefw'.split()).decode('utf-8').split('\n')
for line in lines:
    if '\_ traffic_server' in line:
        pid = line.split()[0].strip()
        output = subprocess.check_output('gdb -n -q -p {0} -x {1}'.format(pid, sys.argv[1]).split()).decode('utf-8').split('\n')
        for o in output:
            print(o)
        break
