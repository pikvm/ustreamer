#!/usr/bin/env -S python3 -B
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

import common


# =====
def main() -> None:
    assert len(sys.argv) == 4, f"{sys.argv[0]} <file.html> <file.h> <name>"
    html_path = sys.argv[1]
    header_path = sys.argv[2]
    name = sys.argv[3]

    with open(html_path, "r") as html_file:
        text = html_file.read()

    text = text.strip()
    text = text.replace("\"", "\\\"")
    text = text.replace("%VERSION%", "\" VERSION \"")
    text = textwrap.indent(text, "\t", (lambda line: True))
    text = "\n".join(
        (f"{line} \\" if line.strip() else f"{line}\\")
        for line in text.split("\n")
    )
    text = f"const char HTML_{name}_PAGE[] = \" \\\n{text}\n\";\n"
    text = f"{common.C_PREPEND}\n#include \"../../config.h\"\n\n\n{text}"

    with open(header_path, "w") as header_file:
        header_file.write(text)


# =====
if __name__ == "__main__":
    main()
