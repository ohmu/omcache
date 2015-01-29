short_ver = 0.2.0
long_ver = $(shell git describe --long 2>/dev/null || echo $(short_ver)-0-unknown-g`git describe --always`)

include compat.mk

PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PYTHON ?= python

STLIB_A = libomcache.a
SHLIB_SO = libomcache.$(SO_EXT)
SHLIB_V = $(SHLIB_SO).0
OBJ = omcache.o commands.o dist.o md5.o util.o


all: $(SHLIB_SO) $(STLIB_A)

$(STLIB_A): $(OBJ)
	ar rc $@ $^
	ranlib $@

$(SHLIB_SO): $(SHLIB_V)
	ln -fs $(SHLIB_V) $(SHLIB_SO)

$(SHLIB_V): $(OBJ) symbol.map
	$(CC) $(LDFLAGS) $(SO_FLAGS) $(filter-out symbol.map,$^) -o $@ $(WITH_LIBS)

install: $(SHLIB_SO)
	mkdir -p $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR)
	cp -a $(SHLIB_SO) $(SHLIB_V) $(DESTDIR)$(LIBDIR)
	cp -a omcache.h omcache_cdef.h omcache_libmemcached.h $(DESTDIR)$(INCLUDEDIR)
ifneq ($(PYTHONDIRS),)
	for pydir in $(PYTHONDIRS); do \
		mkdir -p $(DESTDIR)$$pydir; \
		cp -a omcache.py omcache_pylibmc.py omcache_cdef.h \
			$(DESTDIR)$$pydir; \
	done
endif

rpm:
	git archive --output=omcache-rpm-src.tar.gz --prefix=omcache/ HEAD
	rpmbuild -bb omcache.spec \
		--define '_sourcedir $(shell pwd)' \
		--define 'major_version $(short_ver)' \
		--define 'minor_version $(subst -,.,$(subst $(short_ver)-,,$(long_ver)))'
	$(RM) omcache-rpm-src.tar.gz

deb:
	cp debian/changelog.in debian/changelog
	dch -v $(long_ver) "Automatically built package"
	dpkg-buildpackage -uc -us

clean:
	$(RM) $(STLIB_A) $(SHLIB_V) $(SHLIB_SO) $(OBJ)
	$(MAKE) -C tests clean

check:
	$(MAKE) -C tests check

check-sanitizer:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=address,undefined" \
		LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined" \
		-C tests check

check-valgrind:
	$(MAKE) clean
	$(MAKE)
	$(MAKE) -C tests check CHECKER="valgrind --leak-check=full"

check-coverage:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -fprofile-arcs -ftest-coverage" \
		LDFLAGS="$(LDFLAGS) -fprofile-arcs -ftest-coverage" \
		-C tests check
	gcov -rb $(OBJ:.o=.c)

check-coverity:
	$(MAKE) clean
	$(RM) -r cov-int omcache-cov-int.tar.gz
	cov-build --dir cov-int $(MAKE)
	tar zcvf omcache-cov-int.tar.gz cov-int
	curl --verbose --form 'token=<.coverity-token' \
		--form 'email=<.coverity-email' \
		--form 'file=@omcache-cov-int.tar.gz' \
		--form 'version=$(long_ver)' \
		--form 'description=$(short_ver)' \
		'https://scan.coverity.com/builds?project=saaros%2Fomcache'
	$(RM) -r cov-int omcache-cov-int.tar.gz

check-pylint:
	$(PYTHON) -m pylint --rcfile pylintrc *.py
	PYTHONPATH=. $(PYTHON) -m pylint --rcfile pylintrc tests/python

check-python:
	LD_LIBRARY_PATH=. PYTHONPATH=. $(PYTHON) -m pytest -vv tests/python
