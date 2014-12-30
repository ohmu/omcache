include ../compat.mk

TEST = test_omcache
OBJS = test_omcache.o test_commands.o test_failures.o \
	test_libmcd_compat.o test_misc.o test_servers.o

WITH_CFLAGS += -I..

ifneq ($(WITH_LIBMEMCACHED),)
  WITH_CFLAGS += -DWITH_LIBMEMCACHED
  WITH_LIBS += -lmemcached
endif

all: $(TEST)

check: $(TEST) ../libomcache.$(SO_EXT)
	LD_LIBRARY_PATH=.. $(CHECKER) ./$<

$(TEST): $(OBJS) ../libomcache.a
	$(CC) $(LDFLAGS) $^ -o $@ -lcheck $(WITH_LIBS)

../libomcache.a:
	$(MAKE) -C ..

clean:
	$(RM) $(OBJS) $(TEST)
