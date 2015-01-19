all: $(shell uname -s)

Linux:
	sudo apt-get update
	sudo apt-get install libasyncns-dev check memcached valgrind
	sudo pip install cffi pytest pylint
	$(MAKE) all
	$(MAKE) check-valgrind
	$(MAKE) check-python check-pylint

Darwin:
	brew update
	brew install check || :
	$(MAKE) WITHOUT_ASYNCNS=1 all
	$(MAKE) WITHOUT_ASYNCNS=1 check
