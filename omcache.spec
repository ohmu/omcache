Name:           libomcache
Version:        %{major_version}
Release:        %{minor_version}%{?dist}
Summary:        memcache client library
Group:          System Environment/Libraries
URL:            https://github.com/ohmu/omcache/
License:        ASL 2.0
Source0:        omcache-rpm-src.tar.gz
BuildRequires:  check-devel, libasyncns-devel, memcached

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
BuildRequires:	python-devel, pylint, pytest, python-cffi
Requires:	python-cffi, %{name} = %{version}

%description -n python-omcache
Python 2.x bindings for the OMcache memcache client library.

%if %{?python3_sitelib:1}0
%package -n python3-omcache
Summary:	memcache client library for python 3.x
Group:		Development/Languages
BuildArch:	noarch
BuildRequires:	python3-devel, python3-pylint, python3-pytest, python3-cffi
Requires:	python3-cffi, %{name} = %{version}

%description -n python3-omcache
Python 3.x bindings for the OMcache memcache client library.
%endif

%prep
%setup -q -n omcache

%build
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot} \
    PREFIX=%{_prefix} LIBDIR=%{_libdir} \
    PYTHONDIRS="%{python2_sitelib} %{?python3_sitelib}"

%check
make check
make check-pylint check-python PYTHON=python2
%if %{?python3_sitelib:1}0
make check-pylint check-python PYTHON=python3
%endif

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc README.rst LICENSE NEWS
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

%if %{?python3_sitelib:1}0
%files -n python3-omcache
%defattr(-,root,root,-)
%{python3_sitelib}/omcache*
%{python3_sitelib}/__pycache__/omcache*
%endif

%changelog
* Wed Jan 28 2015 Oskari Saarenmaa <os@ohmu.fi> - 0.2.0-15-g34d33df
- Don't package python3 bindings if python3_sitelib isn't defined
- Run python tests and BuildRequire pytest and python-cffi
- BuildRequire memcached, it's needed for make check

* Mon Oct 13 2014 Oskari Saarenmaa <os@ohmu.fi> - 0-unknown
- Initial.
