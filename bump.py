#!/usr/bin/env python3
# ========================================================================== #
#                                                                            #
#    KVMD - The main PiKVM daemon.                                           #
#                                                                            #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
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
import re
import subprocess


# =====
def _run(cmd: list[str]) -> str:
    return str(subprocess.run(cmd, stdout=subprocess.PIPE, check=True).stdout.decode())


def _get_version() -> tuple[int, int]:
    ver = _run(["/usr/bin/git", "describe", "--tags", "--abbrev=0"]).strip()
    assert (m := re.match(r"v(\d+)\.(\d+)", ver)), ver
    return (int(m.group(1)), int(m.group(2)))


def _make_version(major: int, minor: int) -> str:
    return f"v{major}.{minor}"


def _patch_file(path: str, replace: str, old: tuple[int, int], new: tuple[int, int]) -> None:
    with open(path, "r+") as f:
        old_text = f.read()
        new_text = old_text.replace(
            replace.format(major=old[0], minor=old[1]),
            replace.format(major=new[0], minor=new[1]),
        )
        # assert old_text != new_text, path
        f.seek(0, os.SEEK_SET)
        f.write(new_text)


def _bump(do_major: bool, items: list[tuple[str, str]]) -> None:
    _run(["/usr/bin/git", "diff-index", "--quiet", "HEAD", "--"])

    old = _get_version()
    new = ((old[0] + 1, old[1]) if do_major else (old[0], old[1] + 1))

    for (path, replace) in items:
        _patch_file(path, replace, old, new)
        _run(["/usr/bin/git", "add", path])

    ver_new = _make_version(*new)
    _run(["/usr/bin/git", "commit", "-m", f"Bump version: {_make_version(*old)} -> {ver_new}"])
    _run(["/usr/bin/git", "tag", ver_new])


# =====
if __name__ == "__main__":
    _bump(
        do_major=(len(sys.argv) == 2 and sys.argv[1] == "major"),
        items=[
            ("src/libs/const.h",     "define US_VERSION_MAJOR {major}"),
            ("src/libs/const.h",     "define US_VERSION_MINOR {minor}"),
            ("python/setup.py",      "version=\"{major}.{minor}\""),
            ("man/ustreamer.1",      "version {major}.{minor}"),
            ("man/ustreamer-dump.1", "version {major}.{minor}"),
            ("pkg/arch/PKGBUILD",    "pkgver={major}.{minor}"),
            ("pkg/openwrt/Makefile", "PKG_VERSION:={major}.{minor}"),
        ],
    )
