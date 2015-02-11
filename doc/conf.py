import os

if os.environ.get('READTHEDOCS', None) == 'True':
    import subprocess, sys
    subprocess.call(['doxygen'])
    subprocess.call(["git", "clone", "--depth", "1", "--branch", "v3.2.0", "https://github.com/michaeljones/breathe.git", "../breathe-git"])
    sys.path.append("../breathe-git")
    version = subprocess.check_output(["git", "describe", "--abbrev=0"])
    release = subprocess.check_output(["git", "describe", "--long"])
else:
    version = os.getenv("OMCACHE_VERSION")
    release = os.getenv("OMCACHE_VERSION_FULL")

# generate index.rst from readme, skip the badges, add toc
readme_lines = open("../README.rst").read().splitlines()
readme_content = "\n".join(readme_lines[9:])
beef = """
=======
OMcache
=======

.. toctree::
  :maxdepth: 2

  omcache_cdef

Introduction
============
""" + readme_content
open("index.rst", "w").write(beef)


project = 'OMcache'
copyright = '2013-2014, Oskari Saarenmaa'

extensions = ['breathe']
breathe_projects = {'omcache_c_api': 'omcache_c_api_xml'}
breathe_default_project = 'omcache_c_api'
source_suffix = '.rst'
master_doc = 'index'
pygments_style = 'sphinx'
highlight_language = 'c'
primary_domain = 'c'
html_use_index = False
