all: $(shell uname -s)

Linux:
	sudo apt-get update
	sudo apt-get install libasyncns-dev check memcached valgrind
	pip install cffi pytest pylint
	$(MAKE) all
	# We don't need to run C unit tests under valgrind for every python
	# version, run them only when we're testing Python 2.6 as we'll skip
	# the pylint checks when running it as pylint no longer supports 2.6
	(python -V 2>&1 | grep -qvF 'Python 2.6') || $(MAKE) check-valgrind
	(python -V 2>&1 | grep -qF 'Python 2.6') || $(MAKE) check-pylint
	$(MAKE) check-python

Darwin:
	brew update
	brew install check || :
	$(MAKE) WITHOUT_ASYNCNS=1 all
	$(MAKE) WITHOUT_ASYNCNS=1 check
