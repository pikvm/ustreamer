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
import io
import struct

import common


# =====
def _get_jpeg_size(data: bytes) -> tuple[int, int]:
    # https://sheep.horse/2013/9/finding_the_dimensions_of_a_jpeg_file_in_python.html

    stream = io.BytesIO(data)
    while True:
        marker = struct.unpack(">H", stream.read(2))[0]
        if marker == 0xFFD9:
            raise RuntimeError("Can't find jpeg size")

        if (
            marker == 0xFFD8  # Start of image
            or marker == 0xFF01  # Private marker
            or 0xFFD0 <= marker <= 0xFFD7  # Restart markers
        ):
            continue

        # All other markers specify chunks with lengths
        length = struct.unpack(">H", stream.read(2))[0]

        if marker == 0xFFC0:  # Baseline DCT chunk, has the info we want
            (_, height, width) = struct.unpack(">BHH", stream.read(5))
            return (width, height)

        # Not the chunk we want, skip it
        stream.seek(length - 2, 1)


# =====
def main() -> None:
    assert len(sys.argv) == 4, f"{sys.argv[0]} <file.jpeg> <file.h> <name>"
    jpeg_path = sys.argv[1]
    c_path = sys.argv[2]
    h_path = os.path.basename(c_path[:-2]) + ".h"
    name = sys.argv[3]

    with open(jpeg_path, "rb") as file:
        data = file.read()

    (width, height) = _get_jpeg_size(data)

    data_text = "{\n\t" + ",\n\t".join(
        ", ".join(
            f"0x{ch:02X}"
            for ch in data[index:index + 20]
        )
        for index in range(0, len(data), 20)
    ) + ",\n}"

    text = f"{common.C_PREPEND}\n"
    text += f"#include \"{h_path}\"\n\n\n"
    text += f"const unsigned US_{name}_JPEG_WIDTH = {width};\n"
    text += f"const unsigned US_{name}_JPEG_HEIGHT = {height};\n\n"
    text += f"const size_t US_{name}_JPEG_DATA_SIZE = {len(data)};\n"
    text += f"const uint8_t US_{name}_JPEG_DATA[] = {data_text};\n"

    with open(c_path, "w") as file:
        file.write(text)


# =====
if __name__ == "__main__":
    main()
