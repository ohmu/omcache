SHLIB_SO = libomcache.so
SHLIB_V = $(SHLIB_SO).0
OBJ = omcache.o oconst.o
LDFLAGS = -shared
CPPFLAGS = -Wall -Wextra -D_GNU_SOURCE
CFLAGS = -g -std=gnu99 -fPIC

all: $(SHLIB_SO)

$(SHLIB_SO): $(SHLIB_V)
	ln -fs $(SHLIB_V) $(SHLIB_SO)

$(SHLIB_V): $(OBJ)
	$(CC) $(LDFLAGS) -Wl,-soname,$(SHLIB) -Wl,-version-script,symbol.map $^ -o $@ -lrt

clean:
	$(RM) $(SHLIB_V) $(SHLIB_SO) $(OBJ)
