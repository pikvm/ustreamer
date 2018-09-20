#!/usr/bin/env python3


import sys


# =====
def main():
    assert len(sys.argv) == 6, "%s <src> <dest> <prefix> <width> <height>" % (sys.argv[0])

    src = sys.argv[1]
    dest = sys.argv[2]
    prefix = sys.argv[3]
    width = int(sys.argv[4])
    height = int(sys.argv[5])

    with open(src, "rb") as jpg_file:
        jpg_data = jpg_file.read()

    rows = [[]]
    for ch in jpg_data:
        if len(rows[-1]) > 20:
            rows.append([])
        rows[-1].append(hex(ch))

    text = ",\n\t".join(", ".join(row) for row in rows)
    text = "const unsigned char %s_JPG_DATA[] = {\n\t%s\n};" % (prefix, text)
    text = "const unsigned long %s_JPG_SIZE = %d;\n\n" % (prefix, len(jpg_data)) + text
    text = "const unsigned %s_JPG_HEIGHT = %d;\n\n" % (prefix, height) + text
    text = "const unsigned %s_JPG_WIDTH = %d;\n" % (prefix, width) + text

    with open(dest, "w") as h_file:
        h_file.write(text)


# =====
if __name__ == "__main__":
    main()
