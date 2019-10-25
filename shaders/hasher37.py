#!/usr/bin/python3.7

import hashlib
import sys


def main():
    m = hashlib.sha256();
    m.update(sys.argv[1].encode('utf-8'))
    print(m.hexdigest()[-10:])


if __name__ == "__main__":
    main()

