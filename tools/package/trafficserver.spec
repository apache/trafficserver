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

# I had to disable this on RHEL7, because libunwind is not properly built for -fPIE it seems
%if %{?fedora}0 > 0  || %{?rhel}0 >= 80
%define _hardened_build 1
%endif

# This can be overriden via command line option, e.g.  --define “release 12"
%{!?release: %define release 1}

Summary:	Apache Traffic Server, a reverse, forward and transparent HTTP proxy cache
Name:		trafficserver
Version:	9.0.0
Release:	%{release}%{?dist}
License:	Apache Software License 2.0 (AL2)
Group:		System Environment/Daemons
URL:		https://trafficserver.apache.org/

Source0:	http://www.apache.org/dist/%{name}/%{name}-%{version}.tar.bz2

BuildRequires:	expat-devel hwloc-devel openssl-devel pcre-devel tcl-devel zlib-devel xz-devel
BuildRequires:  libcurl-devel ncurses-devel
BuildRequires:	gcc gcc-c++ perl-ExtUtils-MakeMaker
BuildRequires:  libcap-devel

Requires:	expat hwloc openssl pcre tcl zlib xz libcurl ncurses pkgconfig
Requires:	libcap

# Can't seem to use libunwind on RHEL7 or older
%if %{?fedora}0 > 0  || %{?rhel}0 >= 80
BuildRequires:  libunwind-devel
%else
%define DISABLE_UNWIND "--disable-unwind"
%endif

%if %{?fedora}0 > 0  || %{?rhel}0 >= 70
Requires:	systemd
Requires(postun): systemd
%else
Requires:	initscripts
%endif

%description
Apache Traffic Server is an OpenSource HTTP / HTTPS / HTTP/2 / QUIC reverse,
forward and transparent proxy and cache.

%package devel
Summary: Apache Traffic Server devel package
Group: Development/Libraries
Requires: trafficserver = %{version}-%{release}

%description devel
Include files and various tools for ATS developers.

%package perl
Summary: ATS management Perl bindings
Group: Development/Libraries
Requires: trafficserver = %{version}-%{release}

%description perl
This package contains some Perl APIs for talking to the ATS management port.

%prep

%setup -q

%build
%configure \
  --enable-layout=Gentoo \
  --libdir=%{_libdir}/trafficserver \
  --libexecdir=%{_libdir}/trafficserver/plugins \
  --sysconfdir=%{_sysconfdir}/trafficserver \
  --enable-experimental-plugins \
  --with-user=ats --with-group=ats \
  %{DISABLE_UNWIND} \
  --disable-silent-rules

make %{?_smp_mflags} V=1

%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} install

%if %{?fedora}0 > 0 || %{?rhel}0 >= 70
mkdir -p %{buildroot}/lib/systemd/system
cp rc/trafficserver.service %{buildroot}/lib/systemd/system
%else
mkdir -p %{buildroot}/etc/init.d
mv %{buildroot}%{_bindir}/trafficserver %{buildroot}/etc/init.d
%endif

# Remove libtool archives and static libs
find %{buildroot} -type f -name "*.la" -delete
find %{buildroot} -type f -name "*.a" -delete
find %{buildroot} -type f -name "*.pod" -delete
find %{buildroot} -type f -name "*.in" -delete
find %{buildroot} -type f -name ".packlist" -delete

# ToDo: Why is the Perl stuff ending up in the wrong place ??
mkdir -p %{buildroot}%{_datadir}/perl5
mv %{buildroot}/usr/lib/perl5/* %{buildroot}%{_datadir}/perl5

mkdir -p %{buildroot}/run/trafficserver

mkdir -p %{buildroot}%{_datadir}/pkgconfig
mv %{buildroot}%{_libdir}/trafficserver/pkgconfig/trafficserver.pc %{buildroot}%{_datadir}/pkgconfig

%post
/sbin/ldconfig
%if %{?fedora}0 > 0 || %{?rhel}0 >= 70
%systemd_post trafficserver.service
%endif

# These UID/GIDs are retained from the upstream Fedora .spec, not sure if there's a registry for these?
%pre
getent group ats >/dev/null || groupadd -r ats -g 176 &>/dev/null
getent passwd ats >/dev/null || useradd -r -u 176 -g ats -d / -s /sbin/nologin -c "Apache Traffic Server" ats &>/dev/null

%preun
%if %{?fedora}0 > 0 || %{?rhel}0 >= 70
%systemd_preun trafficserver.service
%endif

%postun
/sbin/ldconfig

%if %{?fedora}0 > 0 || %{?rhel}0 >= 70
%systemd_postun_with_restart trafficserver.service
%endif

%files
%defattr(-, root, root, -)
%{!?_licensedir:%global license %%doc}
%license LICENSE
%doc README CHANGELOG* NOTICE STATUS
%config(noreplace) /etc/trafficserver/*
%{_bindir}/traffic*
%{_bindir}/tspush
%dir %{_libdir}/trafficserver
%dir %{_libdir}/trafficserver/plugins
%{_libdir}/trafficserver/libts*.so*
%{_libdir}/trafficserver/plugins/*.so

%if %{?fedora}0 > 0 || %{?rhel}0 >= 70
/lib/systemd/system/trafficserver.service
%else
%config(noreplace) /etc/init.d/trafficserver
%endif

# Change the default file and directory permissions
%attr(0755, ats, ats) %dir /etc/trafficserver
%attr(0755, ats, ats) %dir /var/log/trafficserver
%attr(0755, ats, ats) %dir /run/trafficserver
%attr(0755, ats, ats) %dir /var/cache/trafficserver
%attr(0644, ats, ats) /etc/trafficserver/*.config
%attr(0644, ats, ats) /etc/trafficserver/*.yaml

%files perl
%defattr(-,root,root,-)
%{_mandir}/man3/*
%{_datadir}/perl5/Apache/*

%files devel
%defattr(-,root,root,-)
%{_bindir}/tsxs
%{_includedir}/ts
%{_includedir}/tscpp
%{_datadir}/pkgconfig/trafficserver.pc

%changelog
* Wed Sep 19 2018 Bryan Call <bcall@apache.org> - 8.0.0-1
- Changed the owner ofthe configuration files to ats
- Include files for the C++ APIs moved
- C++ library name changed

* Tue Dec 19 2017 Leif Hedstrom <zwoop@apache.org> - 7.1.2-1
- Cleanup for 7.1.x, and various other changes. This needs more work
  upstream though, since I'm finding issues.
- Losely based on ideas from the Fedora .spec
