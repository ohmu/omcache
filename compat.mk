UNAME_S = $(shell uname -s)
CPPFLAGS ?= -Wall -Wextra
CFLAGS ?= -g -O2

ifeq ($(UNAME_S),Linux)
  SO_EXT = so
  SO_FLAGS = -shared -fPIC -Wl,-soname=$(SHLIB_V) -Wl,-version-script=symbol.map
  WITH_LIBS += -lrt
  WITH_CFLAGS += -std=gnu99 -D_GNU_SOURCE
else ifeq ($(UNAME_S),SunOS)
  SO_EXT = so
  SO_FLAGS = -shared -fPIC -Wl,-h,$(SHLIB_V) -Wl,-M,symbol.map
  WITH_LIBS += -lrt -lsocket
  ifneq ($(shell $(CC) -V 2>&1 | grep "Sun C"),)
    WITH_CFLAGS += -xc99
  else
    WITH_CFLAGS += -std=gnu99 -D_XOPEN_SOURCE=600 -D__EXTENSIONS__
  endif
else ifeq ($(UNAME_S),Darwin)
  SO_EXT = dylib
  SO_FLAGS = -dynamiclib
  WITH_CFLAGS += -std=gnu99 -D_DARWIN_C_SOURCE
endif

ifeq ($(WITHOUT_ASYNCNS),)
  WITH_CFLAGS += -DWITH_ASYNCNS
  WITH_LIBS += -lasyncns
endif

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WITH_CFLAGS) -fPIC -c $^
