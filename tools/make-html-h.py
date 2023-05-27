#!/usr/bin/env -S python3 -B
# ========================================================================== #
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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
# ========================================================================== #


import sys
import os
import textwrap

import common


# =====
def main() -> None:
    assert len(sys.argv) == 4, f"{sys.argv[0]} <file.html> <file.c> <name>"
    html_path = sys.argv[1]
    c_path = sys.argv[2]
    h_path = os.path.basename(c_path[:-2] + ".h")
    name = sys.argv[3]

    with open(html_path, "r") as file:
        html = file.read()

    html = html.strip()
    html = html.replace("\"", "\\\"")
    html = html.replace("%VERSION%", "\" US_VERSION \"")
    html = textwrap.indent(html, "\t", (lambda line: True))
    html = "\n".join(
        (f"{line} \\" if line.strip() else f"{line}\\")
        for line in html.split("\n")
    )

    text = f"{common.C_PREPEND}\n#include \"{h_path}\"\n\n\n"
    text += f"const char *const US_HTML_{name}_PAGE = \" \\\n{html}\n\";\n"

    with open(c_path, "w") as file:
        file.write(text)


# =====
if __name__ == "__main__":
    main()
