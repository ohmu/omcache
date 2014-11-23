TEST = test_omcache
OBJS = test_omcache.o test_commands.o test_failures.o \
	test_libmcd_compat.o test_misc.o test_servers.o
CPPFLAGS ?= -Wall -Wextra
CFLAGS ?= -g -O2

ifneq ($(WITH_LIBMEMCACHED),)
WITH_CFLAGS += -DWITH_LIBMEMCACHED
WITH_LIBS += -lmemcached
endif

ifeq ($(WITHOUT_ASYNCNS),)
WITH_CFLAGS += -DWITH_ASYNCNS
WITH_LIBS += -lasyncns
endif

all: $(TEST)

check: $(TEST) ../libomcache.so
	LD_LIBRARY_PATH=.. $(CHECKER) ./$<

$(TEST): $(OBJS) ../libomcache.a
	$(CC) $(LDFLAGS) $^ -o $@ -lrt -lcheck $(WITH_LIBS)

../libomcache.a:
	$(MAKE) -C ..

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WITH_CFLAGS) -I.. -D_GNU_SOURCE=1 -std=gnu99 -fPIC -c $^

clean:
	$(RM) $(OBJS) $(TEST)
