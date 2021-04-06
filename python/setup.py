import os

from distutils.core import Extension
from distutils.core import setup


# =====
if __name__ == "__main__":
    setup(
        name="ustreamer",
        version="3.25",
        description="uStreamer tools",
        author="Maxim Devaev",
        author_email="mdevaev@gmail.com",
        url="https://github.com/pikvm/ustreamer",
        ext_modules=[
            Extension(
                "ustreamer",
                libraries=["rt", "m", "pthread"],
                undef_macros=["NDEBUG"],
                sources=[name for name in os.listdir(".") if name.endswith(".c")],
                depends=[name for name in os.listdir(".") if name.endswith(".h")],
            ),
        ],
    )
