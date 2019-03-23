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
import io
import struct

from typing import Tuple
from typing import List

import common


# =====
def _get_jpeg_size(data: bytes) -> Tuple[int, int]:
    # https://sheep.horse/2013/9/finding_the_dimensions_of_a_jpeg_file_in_python.html

    stream = io.BytesIO(data)
    while True:
        marker = struct.unpack(">H", stream.read(2))[0]
        if (
            marker == 0xFFD8  # Start of image
            or marker == 0xFF01  # Private marker
            or (marker >= 0xFFD0 and marker <= 0xFFD7)  # Restart markers
        ):
            continue
        elif marker == 0xFFD9:
            raise RuntimeError("Can't find jpeg size")

        # All other markers specify chunks with lengths
        length = struct.unpack(">H", stream.read(2))[0]

        if marker == 0xFFC0:  # Baseline DCT chunk, has the info we want
            (_, height, width) = struct.unpack(">BHH", stream.read(5))
            return (width, height)

        # Not the chunk we want, skip it
        stream.seek(length - 2, 1)


# =====
def main() -> None:
    assert len(sys.argv) == 4, "%s <file.jpeg> <file.h> <name>" % (sys.argv[0])
    jpeg_path = sys.argv[1]
    header_path = sys.argv[2]
    name = sys.argv[3]

    with open(jpeg_path, "rb") as jpeg_file:
        jpeg_data = jpeg_file.read()

    (width, height) = _get_jpeg_size(jpeg_data)

    rows: List[List[str]] = [[]]
    for ch in jpeg_data:
        if len(rows[-1]) > 20:
            rows.append([])
        rows[-1].append("0x%.2X" % (ch))

    text = ",\n\t".join(", ".join(row) for row in rows)
    text = "const unsigned char %s_JPEG_DATA[] = {\n\t%s\n};\n" % (name, text)
    text = "const unsigned %s_JPEG_HEIGHT = %d;\n\n" % (name, height) + text
    text = "const unsigned %s_JPEG_WIDTH = %d;\n" % (name, width) + text
    text = common.get_prepend() + "\n\n" + text

    with open(header_path, "w") as header_file:
        header_file.write(text)


# =====
if __name__ == "__main__":
    main()
