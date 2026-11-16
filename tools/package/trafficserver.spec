#
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
#

%if %{?fedora}0 > 0  || %{?rhel}0 >= 80
%define _hardened_build 1
%endif

# This can be overridden via command line option, e.g.  --define "release 12"
%{!?release: %define release 1}
%{!?_unitdir: %define _unitdir /usr/lib/systemd/system}

Summary:	Apache Traffic Server, a reverse, forward and transparent HTTP proxy cache
Name:		trafficserver
Version:	10.0.0
Release:	%{release}%{?dist}
License:	Apache Software License 2.0 (AL2)
Group:		System Environment/Daemons
URL:		https://trafficserver.apache.org/

Source0:	http://www.apache.org/dist/%{name}/%{name}-%{version}.tar.bz2

BuildRequires:	cmake >= 3.20 ninja-build gcc gcc-c++
BuildRequires:	hwloc-devel openssl-devel pcre2-devel zlib-devel xz-devel
BuildRequires:	libzstd-devel lz4-devel libcurl-devel ncurses-devel libcap-devel
BuildRequires:	pkgconfig systemd-rpm-macros

Requires:	hwloc openssl pcre2 zlib xz libcurl ncurses pkgconfig libcap
Requires:	libzstd lz4 systemd
Requires(postun): systemd

%if %{?fedora}0 > 0  || %{?rhel}0 >= 80
BuildRequires:	libunwind-devel
%endif

%description
Apache Traffic Server is an OpenSource HTTP / HTTPS / HTTP/2 / HTTP/3 reverse,
forward and transparent proxy and cache.

%package devel
Summary: Apache Traffic Server devel package
Group: Development/Libraries
Requires: trafficserver = %{version}-%{release}

%description devel
Include files and libraries for ATS developers.

%prep
%setup -q

%build
%cmake -B "%{_vpath_builddir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir}/trafficserver \
  -DCMAKE_INSTALL_LIBDIR=%{_libdir}/trafficserver \
  -DCMAKE_INSTALL_LIBEXECDIR=%{_libdir}/trafficserver/plugins \
  -DCMAKE_INSTALL_RUNSTATEDIR=/run/trafficserver \
  -DCMAKE_INSTALL_LOGDIR=%{_localstatedir}/log/trafficserver \
  -DCMAKE_INSTALL_CACHEDIR=%{_localstatedir}/cache/trafficserver \
  -DBUILD_EXPERIMENTAL_PLUGINS=ON \
  -DBUILD_TESTING=OFF \
  -DCMAKE_SKIP_INSTALL_RPATH=ON \
  -DWITH_USER=ats \
  -DWITH_GROUP=ats

%cmake_build

%install
%cmake_install

if [ -d "%{buildroot}%{_prefix}%{_sysconfdir}/trafficserver" ]; then
  mkdir -p "%{buildroot}%{_sysconfdir}/trafficserver"
  mv %{buildroot}%{_prefix}%{_sysconfdir}/trafficserver/* %{buildroot}%{_sysconfdir}/trafficserver/
  rmdir "%{buildroot}%{_prefix}%{_sysconfdir}/trafficserver" "%{buildroot}%{_prefix}%{_sysconfdir}" 2>/dev/null || true
fi

mkdir -p %{buildroot}%{_sysconfdir}/ld.so.conf.d
echo "%{_libdir}/trafficserver" > %{buildroot}%{_sysconfdir}/ld.so.conf.d/trafficserver.conf

install -D -m 0644 "%{_vpath_builddir}/rc/trafficserver.service" %{buildroot}%{_unitdir}/trafficserver.service

mkdir -p %{buildroot}/run/trafficserver
mkdir -p %{buildroot}%{_localstatedir}/log/trafficserver
mkdir -p %{buildroot}%{_localstatedir}/cache/trafficserver

# Remove libtool archives and static libs
find %{buildroot} -type f -name "*.la" -delete
find %{buildroot} -type f -name "*.a" -delete
find %{buildroot} -type f -name "*.pod" -delete
find %{buildroot} -type f -name "*.in" -delete
find %{buildroot} -type f -name ".packlist" -delete
find %{buildroot} -type f -name "plugin_*.so" -delete

%post
/sbin/ldconfig
%systemd_post trafficserver.service

%pre
getent group ats >/dev/null || groupadd -r ats -g 176 &>/dev/null
getent passwd ats >/dev/null || useradd -r -u 176 -g ats -d / -s /sbin/nologin -c "Apache Traffic Server" ats &>/dev/null

%preun
%systemd_preun trafficserver.service

%postun
/sbin/ldconfig
%systemd_postun_with_restart trafficserver.service

%files
%defattr(-, root, root, -)
%{!?_licensedir:%global license %%doc}
%license LICENSE
%doc README.md NOTICE STATUS
%{_bindir}/traffic*
%dir %{_libdir}/trafficserver
%dir %{_libdir}/trafficserver/plugins
%{_libdir}/trafficserver/lib*.so*
%{_libdir}/trafficserver/plugins/*.so
%{_unitdir}/trafficserver.service
%config(noreplace) %{_sysconfdir}/ld.so.conf.d/trafficserver.conf

# Change the default file and directory permissions
%attr(0755, ats, ats) %dir %{_sysconfdir}/trafficserver
%attr(0755, ats, ats) %dir %{_localstatedir}/log/trafficserver
%attr(0755, ats, ats) %dir /run/trafficserver
%attr(0755, ats, ats) %dir %{_localstatedir}/cache/trafficserver
%attr(-, ats, ats) %config(noreplace) %{_sysconfdir}/trafficserver/*

%files devel
%defattr(-,root,root,-)
%{_includedir}/*
%{_libdir}/trafficserver/pkgconfig/*
%{_libdir}/trafficserver/cmake/*

%changelog
* Wed Sep 19 2018 Bryan Call <bcall@apache.org> - 8.0.0-1
- Changed the owner of the configuration files to ats
- Include files for the C++ APIs moved
- C++ library name changed

* Tue Dec 19 2017 Leif Hedstrom <zwoop@apache.org> - 7.1.2-1
- Cleanup for 7.1.x, and various other changes. This needs more work
  upstream though, since I'm finding issues.
- Loosely based on ideas from the Fedora .spec
