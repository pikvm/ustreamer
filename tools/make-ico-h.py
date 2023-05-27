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

import common


# =====
def main() -> None:
    assert len(sys.argv) == 4, f"{sys.argv[0]} <file.ico> <file.h> <name>"
    ico_path = sys.argv[1]
    c_path = sys.argv[2]
    h_path = os.path.basename(c_path[:-2]) + ".h"
    name = sys.argv[3]

    with open(ico_path, "rb") as file:
        data = file.read()

    data_text = "{\n\t" + ",\n\t".join(
        ", ".join(
            f"0x{ch:02X}"
            for ch in data[index:index + 20]
        )
        for index in range(0, len(data), 20)
    ) + ",\n}"

    text = f"{common.C_PREPEND}\n"
    text += f"#include \"{h_path}\"\n\n\n"
    text += f"const size_t US_{name}_ICO_DATA_SIZE = {len(data)};\n"
    text += f"const uint8_t US_{name}_ICO_DATA[] = {data_text};\n"

    with open(c_path, "w") as file:
        file.write(text)


# =====
if __name__ == "__main__":
    main()
