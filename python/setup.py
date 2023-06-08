import os

from setuptools import Extension
from setuptools import setup


# =====
def _find_sources(suffix: str) -> list[str]:
    sources: list[str] = []
    for (root_path, _, names) in os.walk("src"):
        for name in names:
            if name.endswith(suffix):
                sources.append(os.path.join(root_path, name))
    return sources


if __name__ == "__main__":
    setup(
        name="ustreamer",
        version="5.40",
        description="uStreamer tools",
        author="Maxim Devaev",
        author_email="mdevaev@gmail.com",
        url="https://github.com/pikvm/ustreamer",
        ext_modules=[
            Extension(
                "ustreamer",
                libraries=["rt", "m", "pthread"],
                extra_compile_args=["-std=c17", "-D_GNU_SOURCE"],
                undef_macros=["NDEBUG"],
                sources=_find_sources(".c"),
                depends=_find_sources(".h"),
            ),
        ],
    )
