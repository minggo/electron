#!/usr/bin/env python

import sys
import argparse
import os

from lib.util import extract_zip

SOURCE_ROOT = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))

def main():
  os.chdir(SOURCE_ROOT)

  args = parse_args()
  extract_zip(args.src, args.output);
  print "extract complete: " + args.src;


def parse_args():
  parser = argparse.ArgumentParser(description='Extract zip file')
  parser.add_argument('-s', '--src',
                      help='The source zip file to extract from',
                      required=True)
  parser.add_argument('-o', '--output',
                      help='The destination location to extract to',
                      required=True)
  return parser.parse_args()


if __name__ == '__main__':
  sys.exit(main())