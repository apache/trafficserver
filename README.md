TEST TEST TEST DO NOT COMMIT
# Apache Traffic Server

[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-centos_7%2F&label=CentOS%207)](https://ci.trafficserver.apache.org/job/master/job/os-centos_7/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-debian_10%2F&label=Debian%2010)](https://ci.trafficserver.apache.org/job/master/job/os-debian_10/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-debian_11%2F&label=Debian%2011)](https://ci.trafficserver.apache.org/job/master/job/os-debian_11/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-fedora_37%2F&label=Fedora%2037)](https://ci.trafficserver.apache.org/job/master/job/os-fedora_37/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-fedora_38%2F&label=Fedora%2038)](https://ci.trafficserver.apache.org/job/master/job/os-fedora_38/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Ffreebsd%2F&label=FreeBSD)](https://ci.trafficserver.apache.org/job/master/job/freebsd/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fosx%2F&label=macOS)](https://ci.trafficserver.apache.org/job/master/job/osx/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fosx-m1%2F&label=macOS%20arm64)](https://ci.trafficserver.apache.org/job/master/job/osx-m1/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-rockylinux_8%2F&label=Rocky%20Linux%208)](https://ci.trafficserver.apache.org/job/master/job/os-rockylinux_8/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-rockylinux_8%2F&label=Rocky%20Linux%209)](https://ci.trafficserver.apache.org/job/master/job/os-rockylinux_9/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-ubuntu_20.04%2F&label=Ubuntu%2020.04)](https://ci.trafficserver.apache.org/job/master/job/os-ubuntu_20.04/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-ubuntu_22.04%2F&label=Ubuntu%2022.04)](https://ci.trafficserver.apache.org/job/master/job/os-ubuntu_22.04/)
[![Jenkins](https://img.shields.io/jenkins/build?jobUrl=https%3A%2F%2Fci.trafficserver.apache.org%2Fjob%2Fmaster%2Fjob%2Fos-ubuntu_23.04%2F&label=Ubuntu%2023.04)](https://ci.trafficserver.apache.org/job/master/job/os-ubuntu_23.04/)

Traffic Server is a high-performance building block for cloud services.
It's more than just a caching proxy server; it also has support for
plugins to build large scale web applications.

# Important notice to ATS developers

ATS is transitioning to cmake as its build system.  At the moment, the autotools build is broken and will soon be removed from the repository.  Below is a quick-start guide to cmake:

### Step 1: Configuration

With cmake, you definitely want to create an out-of-source build.  You will give that directory to every cmake command.  For these examples, it will just be `build`

```
$ cmake -B build
```

This will configure the project with defaults.

If you want to customize the build, you can pass values for variables on the command line.  Or, you can interactively change them using the `ccmake` program.

```
$ cmake -B build -DCMAKE_INSTALL_PREFIX=/tmp/ats -DBUILD_EXPERIMENTAL_PLUGINS=ON
```

-- or --

```
$ ccmake build
```

#### Specifying locations of dependencies

To specify the location of a dependency (like --with-*), you generally set a variable with the `ROOT`. The big exception to this is for openssl. This variable is called `OPENSSL_ROOT_DIR`

```
$ cmake -B build -Djemalloc_ROOT=/opt/jemalloc -DPCRE_ROOT=/opt/edge -DOPENSSL_ROOT_DIR=/opt/boringssl
```

#### Using presets to configure the build

cmake has a feature for grouping configurations together to make configuration and reproduction easier.  The file CMakePresets.json declares presets that you can use from the command line.  You can provide your own CMakeUserPresets.json and further refine those via inheritance:

```
$ cmake --preset dev
```

You can start out your user presets by just copying `CMakePresets.json` and removing everything in `configurePresets`

Here is an example user preset:

```

    {
      "name": "clang",
      "hidden": true,
      "environment": {
        "LDFLAGS": "-L/opt/homebrew/opt/llvm/lib -L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++ -fuse-ld=/opt/homebrew/opt/llvm/bin/ld64.lld",
        "CPPFLAGS": "-I/opt/homebrew/opt/llvm/include",
        "CXXFLAGS": "-stdlib=libc++",
        "CC": "/opt/homebrew/opt/llvm/bin/clang",
        "CXX": "/opt/homebrew/opt/llvm/bin/clang++"
      }
    },
    {
      "name": "mydev",
      "displayName": "my development",
      "description": "My Development Presets",
      "binaryDir": "${sourceDir}/build-dev-clang",
      "inherits": ["clang", "dev"],
      "cacheVariables": {
        "CMAKE_INSTALL_PREFIX": "/opt/ats-cmake",
        "jemalloc_ROOT": "/opt/homebrew",
        "ENABLE_LUAJIT": false,
        "ENABLE_JEMALLOC": true,
        "ENABLE_MIMALLOC": false,
        "ENABLE_MALLOC_ALLOCATOR": true,
        "BUILD_EXPERIMENTAL_PLUGINS": true,
        "BUILD_REGRESSION_TESTING": true
      }
    },
```

And then use it like:

```
cmake --preset mydev
```

## Building the project

```
$ cmake --build build
```

```
$ cmake --build build -t traffic_server
```

## running tests

```
$ cd build
$ ctest
```

## installing

```
$ cmake --install build
```

## DIRECTORY STRUCTURE
```
trafficserver ............. Top src dir
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
│   ├── records ........... Library for config files
│   └── yamlcpp ........... Library for YAML of C++
├── m4 .................... Custom macros for configure.ac
├── mk .................... Includes for Makefiles
├── mgmt .................. JSONRPC server/management and tools
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
│   ├── traffic_server .... Main proxy server
│   ├── traffic_top ....... Top like tool for viewing Traffic Server statistics
│   ├── traffic_via ....... Tool for decoding the Traffic Server Via header codes
│   ├── tscore ............ Base / core library
│   ├── tscpp ............. C++ api wrapper for plugin developers
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

  As of ATS v9.0.0 and later, gcc 7 or later is required, since we now use and require the C++17 standard.

### Fedora / CentOS / RHEL:
```
cmake
ninja
pkgconfig
gcc/g++ or clang/clang++
openssl-devel
pcre-devel
ncurses-devel and libcurl-devel(optional, needed for traffic_top)
libcap-devel (optional, highly recommended)
hwloc-devel (optional, highly recommended)
```

### Ubuntu / Debian
```
cmake
ninja
pkg-config
gcc/g++ or clang/clang++
zlib1g-dev
libssl-dev
libpcre3-dev
libcap-dev (optional, highly recommended)
libhwloc-dev (optional, highly recommended)
libncurses5-dev (optional, required for e.g.: traffic_top)
libcurl4-openssl-dev (optional, required for e.g.: traffic_top)
```

### Alpine Linux
```
build-base
libexecinfo-dev
pcre-dev
libressl-dev
cmake
ninja
linux-headers
```

### macOS (we recommend HomeBrew):
```
cmake
ninja
pkg-config
openssl
pcre
```

### FreeBSD
```
cmake
ninja
devel/gmake
devel/pkgconf
security/openssl
devel/pcre
textproc/flex (optional, install newer version from ports, fix PATH)
devel/hwloc (optional, highly recommended)
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
cmake -B build
cmake --build build
```

This will build with a destination prefix of /usr/local. You can finish
the installation with

```
sudo cmake --install build
```


## BUILDING FROM GIT REPO

```
git clone https://github.com/apache/trafficserver.git   # get the source code from ASF Git repository
cd trafficserver                                        # enter the checkout directory
cmake --preset default                                  # configure the build
cmake --build build-default                             # execute the compile
cmake --build build-default -t test                     # run tests (optional)
cmake --install build-default                           # install
```

## Instructions for building on EC2
NOTE: Alternately you may use the scripts under 'contrib' which will automate the install for trafficserver under EC2 which is HIGHLY RECOMMENDED. See 'README-EC2' for further details.

### As root do the following when using Ubuntu
```
mkdir -p /mnt          #EC2 Storage Mount, where storage is located
cd /mnt
git clone ...          # get the source code from ASF Git repo
cd trafficserver       # enter the checkout dir
cmake --preset default                                  # configure the build
cmake --build build-default                                   # execute the compile
cmake --build build-default -t test
cmake --install build-default
```

### As root do the following when using Fedora Core 8 kernel
```
mkdir -p /mnt                             #EC2 Storage Mount, where storage is located
cd /mnt
git clone ...                             # get the source code from ASF Git repo
cd trafficserver                          # enter the checkout dir
cmake --preset default                    # configure the build
cmake --build build-default               # execute the compile
cmake --build build-default -t test       # run tests (optional)
cmake --install build-default             # install
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

## Fuzzing

### FLAGS

```bash
export CC=clang
export CXX=clang++
export CFLAGS="-O1 -fno-omit-frame-pointer -gline-tables-only -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -fsanitize=address -fsanitize-address-use-after-scope -fsanitize=fuzzer-no-link"
export CXXFLAGS="-O1 -fno-omit-frame-pointer -gline-tables-only -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -fsanitize=address -fsanitize-address-use-after-scope -fsanitize=fuzzer-no-link"
export LIB_FUZZING_ENGINE=-fsanitize=fuzzer
```

### Compile

```bash
mkdir -p build && cd build/
cmake -DENABLE_POSIX_CAP=OFF -DENABLE_FUZZING=ON -DYAML_BUILD_SHARED_LIBS=OFF ../.
make -j$(nproc)
```

## ADDITIONAL INFO

- Web page: https://trafficserver.apache.org/
- Wiki: https://cwiki.apache.org/confluence/display/TS/
- User mailing list: users@trafficserver.apache.org
