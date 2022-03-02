import os

from setuptools import Extension
from setuptools import setup


# =====
if __name__ == "__main__":
    setup(
        name="ustreamer",
        version="5.0",
        description="uStreamer tools",
        author="Maxim Devaev",
        author_email="mdevaev@gmail.com",
        url="https://github.com/pikvm/ustreamer",
        ext_modules=[
            Extension(
                "ustreamer",
                libraries=["rt", "m", "pthread"],
                undef_macros=["NDEBUG"],
                sources=["src/" + name for name in os.listdir("src") if name.endswith(".c")],
                depends=["src/" + name for name in os.listdir("src") if name.endswith(".h")],
            ),
        ],
    )
