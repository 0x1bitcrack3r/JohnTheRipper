#!/usr/bin/env python

import binascii
import sys

try:
    # FIXME don't depend on passlib
    from passlib.utils import h64
except ImportError:
    sys.stderr.write("Please install passlib python module!\n")

try:
    import optparse
except ImportError:
    sys.stderr.write("Stop living in the past. Upgrade your python!\n")


# NOTE: this map is taken from passlib itself!
_transpose_map = [12, 6, 0, 13, 7, 1, 14, 8, 2, 15, 9, 3, 5, 10, 4, 11]

def process_file(filename, is_standard):
    try:
        fd = open(filename, "rb")
    except IOError:
        e = sys.exc_info()[1]
        sys.stderr.write("%s\n" % str(e))
        return

    username = "?"

    for line in fd.readlines():
        line = line.rstrip('\n')
        if line.endswith(':'):
            username = line.split(':')[0]

        if "password = " in line and "smd5" in line:
            h = line.split("=")[1].lstrip().rstrip()
            if len(h) != 37:
                continue
            if is_standard:
                sys.stdout.write("%s:$1$%s\n" % (username, h[6:]))
            else:
                sys.stdout.write("%s:%s\n" % (username,
                    h))

        elif "password = " in line and "ssha" in line:
            h = line.split("=")[1].lstrip().rstrip()

            tc, salt, h = h.split('$')

            sys.stdout.write("%s:%s$%s$%s\n" % (username,
                    tc, salt, h))

        elif "password = " in line:  # DES
            h = line.split("=")[1].lstrip().rstrip()
            if h != "*":
                sys.stdout.write("%s:%s\n" % (username, h))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.stderr.write("Usage: %s [-s] <AIX passwd file(s) "
            "(/etc/security/passwd)>\n" % sys.argv[0])
        sys.exit(-1)

parser = optparse.OptionParser()
parser.add_option('-s', action="store_true",
                  default=False,
                  dest="is_standard",
                  help='Use this option if "lpa_options '
                        '= std_hash=true" is activated'
                  )
options, remainder = parser.parse_args()

for f in remainder:
    process_file(f, options.is_standard)
