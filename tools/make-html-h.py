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
    assert len(sys.argv) == 4, "%s <src> <dest> <name>" % (sys.argv[0])

    src = sys.argv[1]
    dest = sys.argv[2]
    name = sys.argv[3]

    with open(src, "r") as html_file:
        text = html_file.read()

    text = text.strip()
    text = text.replace("\"", "\\\"")
    text = text.replace("%VERSION%", "\" VERSION \"")
    text = textwrap.indent(text, "\t", (lambda line: True))
    text = "\n".join(("%s \\" if line.strip() else "%s\\") % (line) for line in text.split("\n"))
    text = "const char %s[] = \" \\\n%s\n\";\n" % (name, text)
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


        #pragma once

        #include "../config.h"
    """).strip() + "\n\n\n" + text

    with open(dest, "w") as h_file:
        h_file.write(text)


# =====
if __name__ == "__main__":
    main()
