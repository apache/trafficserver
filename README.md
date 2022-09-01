# Apache Traffic Server

[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-centos_7%2F&label=CentOS%207)](https://ci.trafficserver.apache.org/job/master/job/os-centos_7/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-debian_10%2F&label=Debian%2010)](https://ci.trafficserver.apache.org/job/master/job/os-debian_10/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-debian_11%2F&label=Debian%2011)](https://ci.trafficserver.apache.org/job/master/job/os-debian_11/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-fedora_35%2F&label=Fedora%2035)](https://ci.trafficserver.apache.org/job/master/job/os-fedora_35/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-fedora_36%2F&label=Fedora%2036)](https://ci.trafficserver.apache.org/job/master/job/os-fedora_36/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Ffreebsd%2F&label=FreeBSD)](https://ci.trafficserver.apache.org/job/master/job/freebsd/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fosx%2F&label=macOS)](https://ci.trafficserver.apache.org/job/master/job/osx/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-rockylinux_8%2F&label=Rocky%20Linux%208)](https://ci.trafficserver.apache.org/job/master/job/os-rockylinux_8/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-ubuntu_20.04%2F&label=Ubuntu%2020.04)](https://ci.trafficserver.apache.org/job/master/job/os-ubuntu_20.04/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-ubuntu_21.04%2F&label=Ubuntu%2021.04)](https://ci.trafficserver.apache.org/job/master/job/os-ubuntu_21.04/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-ubuntu_22.04%2F&label=Ubuntu%2022.04)](https://ci.trafficserver.apache.org/job/master/job/os-ubuntu_22.04/)

Traffic Server is a high-performance building block for cloud services.
It's more than just a caching proxy server; it also has support for
plugins to build large scale web applications.

## DIRECTORY STRUCTURE
```
trafficserver ............. Top src dir
├── build ................. Custom macros for configure.ac
├── ci .................... Quality assurance and other CI tools and configs
├── configs ............... Configurations
├── contrib ............... Various contributed auxiliary pieces
├── doc ................... Documentation for Traffic Server
│   ├── admin-guide ....... Admin guide documentations
│   ├── appendices ........ Appendices of Traffic Server
│   ├── developer-guide ... Documentation for developers
│   ├── dot ............... Graphviz source files for docs pictures
│   ├── static ............ Static resources
│   └── uml ............... Documentation in UML
├── example ............... Example plugins
├── iocore ................
│   ├── aio ............... Asynchronous I/O core
│   ├── cache ............. Disk and RAM cache
│   ├── dns ............... DNS (asynchronous)
│   ├── eventsystem ....... Event Driven Engine
│   ├── hostdb ............ Internal DNS cache
│   ├── net ............... Network
│   │   └── quic .......... QUIC implementation
│   └── utils ............. Utilities
├── lib ...................
│   ├── perl .............. Perl libraries for e.g. mgmt access and configurations
│   ├── records ........... Library for config files
│   └── yamlcpp ........... Library for YAML of C++
├── mgmt .................. Management server and tools
├── plugins ............... Stable core plugins
│   └── experimental ...... Experimental core plugins
├── proxy ................. HTTP proxy logic
│   ├── hdrs .............. Headers parsing and management
│   ├── http .............. The actual HTTP protocol implementation
│   ├── http2 ............. HTTP/2 implementation
│   ├── http3 ............. HTTP/3 implementation
│   ├── logging ........... Flexible logging
│   └── shared ............ Shared files
├── rc .................... Installation programs and scripts
├── src ................... Source for all the main binaries / applications
│   ├── traffic_cache_tool  Tool to interact with the Traffic Server cache
│   ├── traffic_crashlog .. Helper process that catches Traffic Server crashes
│   ├── traffic_ctl ....... Command line management tool
│   ├── traffic_layout .... Display information on the build and runtime directory structure
│   ├── traffic_logcat .... Convert binary log file to plain text
│   ├── traffic_logstats .. Log parsing and metrics calculation utility
│   ├── traffic_manager ... The manager process for Traffic Server
│   ├── traffic_server .... Main proxy server
│   ├── traffic_top ....... Top like tool for viewing Traffic Server statistics
│   ├── traffic_via ....... Tool for decoding the Traffic Server Via header codes
│   ├── traffic_wccp ...... Program speaking the client side of the WCCP
│   ├── tscore ............ Base / core library
│   ├── tscpp ............. C++ api wrapper for plugin developers
│   └── wccp .............. WCCP implementation
├── tests ................. Different tests for Traffic Server
├── tools ................. Directory of various tools
├── INSTALL ............... Build and installation guide
├── LAYOUT ................ Traffic Server default layout
├── LICENSE ............... Full license text
├── NOTICE ................ Copyright notices
├── README ................ Intro, links, build info
├── README-EC2 ............ Info on EC2 support
├── REVIEWERS ............. (Incomplete) list of areas with committer interest
└── STATUS ................ Release history and information
```
## REQUIREMENTS

  This section outlines build requirements for different OS
  distributions. This may be out of date compared to the on-line
  requirements at

  <https://cwiki.apache.org/confluence/display/TS/Building>.

  As of ATS v7.0.0 and later, gcc 4.8.1 or later is required, since we now use
  and require the C++11 standard.

### Fedora / CentOS / RHEL:
```
autoconf
automake
libtool
pkgconfig
perl-ExtUtils-MakeMaker
gcc/g++ or clang/clang++
openssl-devel
pcre-devel
ncurses-devel and libcurl-devel(optional, needed for traffic_top)
libcap-devel (optional, highly recommended)
hwloc-devel (optional, highly recommended)
flex (optional, needed for e.g. WCCP)
```

### Ubuntu / Debian
```
autoconf
automake
libtool
pkg-config
libmodule-install-perl
gcc/g++ or clang/clang++
zlib1g-dev
libssl-dev
libpcre3-dev
libcap-dev (optional, highly recommended)
libhwloc-dev (optional, highly recommended)
libncurses5-dev (optional, required for e.g.: traffic_top)
libcurl4-openssl-dev (optional, required for e.g.: traffic_top)
flex (optional, required for e.g. WCCP)
```

### Alpine Linux
```
build-base
libexecinfo-dev
pcre-dev
libressl-dev
autoconf
automake
libtool
linux-headers
```

### macOS (we recommend HomeBrew):
```
autoconf
automake
pkg-config
libtool
openssl
pcre
```

### FreeBSD
```
devel/gmake
devel/autoconf
devel/automake
devel/pkgconf
devel/libtool
security/openssl
devel/pcre
textproc/flex (optional, install newer version from ports, fix PATH)
devel/hwloc (optional, highly recommended)
```

### OmniOS:
```
developer/gcc46
developer/build/gnu-make
developer/build/autoconf
developer/build/automake-111
developer/build/libtool
library/security/openssl
library/pcre
```

## Building from distribution

You can download the latest source code from the official Apache Traffic
Server site:

[https://trafficserver.apache.org/downloads](https://trafficserver.apache.org/downloads)

(or via the URL shortener: http://s.apache.org/uG). Once downloaded,
follow the instructions:

```
tar jxvf trafficserver-9.1.3.tar.bz2
cd trafficserver-9.1.3
./configure            # configure the build environment to create Makefiles
make                   # execute the compile
```

This will build with a destination prefix of /usr/local. You can finish
the installation with

```
sudo make install
```


## BUILDING FROM GIT REPO

```
git clone https://github.com/apache/trafficserver.git   # get the source code from ASF Git repository
cd trafficserver                                        # enter the checkout directory
autoreconf -if                                          # generate the configure script and Makefile.in files
./configure                                             # configure the build environment to create Makefiles
make                                                    # execute the compile
```

## Instructions for building on EC2
NOTE: Alternately you may use the scripts under 'contrib' which will automate the install for trafficserver under EC2 which is HIGHLY RECOMMENDED. See 'README-EC2' for further details.

### As root do the following when using Ubuntu
```
mkdir -p /mnt          #EC2 Storage Mount, where storage is located
cd /mnt
git clone ...          # get the source code from ASF Git repo
cd trafficserver       # enter the checkout dir
autoreconf -i --force  # generate the configure script and Makefile.in files
./configure
make
```

### As root do the following when using Fedora Core 8 kernel
```
mkdir -p /mnt          #EC2 Storage Mount, where storage is located
cd /mnt
git clone ...          # get the source code from ASF Git repo
cd trafficserver       # enter the checkout dir
autoreconf -i --force  # generate the configure script and Makefile.in files
./configure --disable-eventfd
make
```

### Instructions for building on FreeBSD

The only difference is how to run configure and make:
```
MAKE=gmake ./configure # make sure that gmake is the make we use
gmake
```

## INSTALLATION

```
/usr/local
├── /var/log/trafficserver   log files created at runtime
├── /var/trafficserver       runtime files
├── /etc/trafficserver       configuration files
├── /bin                     executable binaries
└── /libexec/trafficserver   plugins
```

## CRYPTO NOTICE

This distribution includes cryptographic software.  The country in
which you currently reside may have restrictions on the import,
possession, use, and/or re-export to another country, of
encryption software.  BEFORE using any encryption software, please
check your country's laws, regulations and policies concerning the
import, possession, or use, and re-export of encryption software, to
see if this is permitted.  See <http://www.wassenaar.org/> for more
information.

The U.S. Government Department of Commerce, Bureau of Industry and
Security (BIS), has classified this software as Export Commodity
Control Number (ECCN) 5D002.C.1, which includes information security
software using or performing cryptographic functions with asymmetric
algorithms.  The form and manner of this Apache Software Foundation
distribution makes it eligible for export under the License Exception
ENC Technology Software Unrestricted (TSU) exception (see the BIS
Export Administration Regulations, Section 740.13) for both object
code and source code.

The following provides more details on the included cryptographic
software:

> The functionality of OpenSSL <http://www.openssl.org/> is
> utilized in parts of the software.

## ADDITIONAL INFO

- Web page: https://trafficserver.apache.org/
- Wiki: https://cwiki.apache.org/confluence/display/TS/
- User mailing list: users@trafficserver.apache.org
