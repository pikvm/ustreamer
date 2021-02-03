from distutils.core import Extension
from distutils.core import setup


# =====
if __name__ == "__main__":
    setup(
        name="ustreamer",
        version="3.16",
        description="uStreamer tools",
        author="Maxim Devaev",
        author_email="mdevaev@gmail.com",
        url="https://github.com/pikvm/ustreamer",
        ext_modules=[
            Extension(
                "ustreamer",
                libraries=["rt", "m", "pthread"],
                undef_macros=["NDEBUG"],
                sources=["ustreamer.c"],
                depends=[
                    "../src/libs/tools.h",
                    "../src/libs/memsinksh.h",
                ],
            ),
        ],
    )
