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
omnios)
    export PATH=/usr/gnu/bin:/usr/bin:/usr/sbin:/sbin:/opt/gcc-4.8.1/bin
    echo "export PATH=/usr/gnu/bin:/usr/bin:/usr/sbin:/sbin:/opt/gcc-4.8.1/bin" >> /root/.profile
    RC=0
    pkg install developer/gcc48 \
        developer/build/autoconf \
        developer/build/automake \
        developer/lexer/flex \
        developer/parser/bison \
        developer/object-file \
        developer/linker \
        developer/library/lint \
        developer/build/gnu-make \
        developer/build/libtool \
        library/idnkit \
        library/idnkit/header-idnkit \
        system/header \
        system/library/math \
        archiver/gnu-tar \
        || RC=${?}
    if [[ ${RC} != 0 ]] && [[ ${RC} != 4 ]]; then
        exit 1
    fi
    if [[ ! -d /usr/local/lib/tcl8.6 ]]; then
        rm -f tcl8.6.4-src.tar.gz
        echo "downloading tcl"
        wget -c -q ftp://ftp.tcl.tk/pub/tcl/tcl8_6/tcl8.6.4-src.tar.gz
        echo "extracting tcl"
        gtar xzf tcl8.6.4-src.tar.gz
        cd tcl8.6.4/unix
        echo "building tcl"
        ./configure --prefix=/usr/local >configure.out 2>&1 || (cat configure.out && exit 1)
        make >make.out 2>&1 || (cat make.out && exit 1)
        echo "installing tcl"
        make install >make.install.out 2>&1 || (cat make.install.out && exit 1)
    else
        echo "tcl is already installed"
    fi
;;
*)
    echo "no config for ${1}"
    exit 1
;;
esac

exit 0
