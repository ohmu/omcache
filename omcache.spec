Name:           libomcache
Version:        %{major_version}
Release:        %{minor_version}%{?dist}
Summary:        memcache client library
Group:          System Environment/Libraries
URL:            https://github.com/saaros/omcache/
License:        ASL 2.0
Source0:        omcache-rpm-src.tar.gz
BuildRequires:  check-devel

%description
OMcache is a low level C library for accessing memcached servers.  The goals
of the OMcache project are stable API and ABI and 'easy' integration into
complex applications and systems; OMcache specifically does not mask any
signals or call any blocking functions.

%package devel
Summary:	development files for omcache
Group:		Development/Libraries
Requires:	%{name} = %{version}

%description devel
Development libraries and headers for the OMcache memcache client library.

%package -n python-omcache
Summary:	memcache client library for python 2.x
Group:		Development/Languages
BuildArch:	noarch
BuildRequires:	python-devel
Requires:	python-cffi, %{name} = %{version}

%description -n python-omcache
Python 2.x bindings for the OMcache memcache client library.

%package -n python3-omcache
Summary:	memcache client library for python 3.x
Group:		Development/Languages
BuildArch:	noarch
BuildRequires:	python3-devel
Requires:	python3-cffi, %{name} = %{version}

%description -n python3-omcache
Python 3.x bindings for the OMcache memcache client library.

%prep
%setup -q -n omcache

%build
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot} \
    PREFIX=%{_prefix} LIBDIR=%{_libdir} \
    PYTHONDIRS="%{python2_sitelib} %{python3_sitelib}"

%check
make check

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc README.rst LICENSE
%{_libdir}/libomcache.so.0

%files devel
%defattr(-,root,root,-)
%{_libdir}/libomcache.so
%{_includedir}/omcache.h
%{_includedir}/omcache_cdef.h
%{_includedir}/omcache_libmemcached.h

%files -n python-omcache
%defattr(-,root,root,-)
%{python2_sitelib}/omcache*

%files -n python3-omcache
%defattr(-,root,root,-)
%{python3_sitelib}/omcache*
%{python3_sitelib}/__pycache__/omcache*

%changelog
* Mon Oct 13 2014 Oskari Saarenmaa <os@ohmu.fi> - 0-unknown
- Initial.
