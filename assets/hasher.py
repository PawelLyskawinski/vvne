#!/usr/bin/python2.7

import hashlib
import sys

print hashlib.sha256(sys.argv[1]).hexdigest()[-10:]