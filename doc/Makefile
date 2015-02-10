include ../version.mk

export OMCACHE_VERSION = $(short_ver)
export OMCACHE_VERSION_FULL = $(long_ver)

all:
	doxygen
	sphinx-build . html
