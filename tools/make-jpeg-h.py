#!/usr/bin/env python3
#============================================================================#
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
#============================================================================#


import sys
import textwrap


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
    text = "const unsigned char %s_JPG_DATA[] = {\n\t%s\n};\n" % (prefix, text)
    text = "const unsigned long %s_JPG_SIZE = %d;\n\n" % (prefix, len(jpg_data)) + text
    text = "const unsigned %s_JPG_HEIGHT = %d;\n\n" % (prefix, height) + text
    text = "const unsigned %s_JPG_WIDTH = %d;\n" % (prefix, width) + text
    text = textwrap.dedent("""
        /*****************************************************************************
        #                                                                            #
        #    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
        #                                                                            #
        #    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
        #                                                                            #
        #    This program is free software: you can redistribute it and/or modify    #
        #    it under the terms of the GNU General Public License as published by    #
        #    the Free Software Foundation, either version 3 of the License, or       #
        #    (at your option) any later version.                                     #
        #                                                                            #
        #    This program is distributed in the hope that it will be useful,         #
        #    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
        #    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
        #    GNU General Public License for more details.                            #
        #                                                                            #
        #    You should have received a copy of the GNU General Public License       #
        #    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
        #                                                                            #
        *****************************************************************************/
    """).strip() + "\n\n\n" + text

    with open(dest, "w") as h_file:
        h_file.write(text)


# =====
if __name__ == "__main__":
    main()
