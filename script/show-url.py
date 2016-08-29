#!/usr/bin/env python

import sys

from lib.config import LIBCHROMIUMCONTENT_COMMIT, BASE_URL, get_target_arch

SHARED_LIBRARY_FILENAME = 'libchromiumcontent.zip'
STATIC_LIBRARY_FILENAME = 'libchromiumcontent-static.zip'

PLATFORM_KEY = {
  'cygwin': 'win',
  'darwin': 'osx',
  'linux2': 'linux',
  'win32': 'win',
}[sys.platform]

shared_url = '{0}/{1}/{2}/{3}/{4}'.format(BASE_URL, PLATFORM_KEY, get_target_arch(), LIBCHROMIUMCONTENT_COMMIT, SHARED_LIBRARY_FILENAME)
static_url = '{0}/{1}/{2}/{3}/{4}'.format(BASE_URL, PLATFORM_KEY, get_target_arch(), LIBCHROMIUMCONTENT_COMMIT, STATIC_LIBRARY_FILENAME)

print('shared library url is: %s' % shared_url)
print
print("static library url is: %s" % static_url)
