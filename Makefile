STLIB_A = libomcache.a
SHLIB_SO = libomcache.so
SHLIB_V = $(SHLIB_SO).0
OBJ = omcache.o commands.o
CPPFLAGS ?= -Wall -Wextra
CFLAGS ?= -g

all: $(SHLIB_SO) $(STLIB_A)

$(STLIB_A): $(OBJ)
	ar rc $@ $^
	ranlib $@

$(SHLIB_SO): $(SHLIB_V)
	ln -fs $(SHLIB_V) $(SHLIB_SO)

$(SHLIB_V): $(OBJ) symbol.map
	$(CC) $(LDFLAGS) -shared -fPIC \
		-Wl,-soname=$(SHLIB_V) -Wl,-version-script=symbol.map \
		$(filter-out symbol.map,$^) -o $@ -lrt

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_GNU_SOURCE=1 -std=gnu99 -fPIC -c $^

clean:
	$(RM) $(STLIB_A) $(SHLIB_V) $(SHLIB_SO) $(OBJ)
