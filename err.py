from __future__ import print_function
import sys

def warning(*objs):
    print("WARNING:", *objs, file=sys.stderr)

warning("THIS IS PRINTED ON STDERR")
