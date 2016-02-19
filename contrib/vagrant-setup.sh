#!/bin/bash

set -e
set -o pipefail

case ${1} in
trusty*|jessie*)
    sed -i 's/^mesg n$/tty -s \&\& mesg n/g' /root/.profile
    apt-get update
    apt-get install -y build-essential autoconf libpcre3-dev libssl-dev tcl-dev libxml2-dev
;;
centos*)
    yum install -y autoconf gcc gcc-c++ pcre-devel openssl-devel tcl-devel libxml2-devel
;;
omnios*)
    sed -i 's/^mesg n$/tty -s \&\& mesg n/g' /root/.profile
;;
*)
    echo "no config for ${1}"
;;
esac

exit 0
