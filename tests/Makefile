TEST = test_omcache
OBJS = test_omcache.o test_commands.o test_failures.o test_servers.o
CPPFLAGS ?= -Wall -Wextra
CFLAGS ?= -g -O2

all: $(TEST)

check: $(TEST) ../libomcache.so
	LD_LIBRARY_PATH=.. ./$<

$(TEST): $(OBJS) ../libomcache.so
	$(CC) $^ -o $@ -lcheck -L.. -lomcache

../libomcache.so:
	$(MAKE) -C ..

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I.. -D_GNU_SOURCE=1 -std=gnu99 -fPIC -c $^

clean:
	$(RM) $(OBJS) $(TEST)
