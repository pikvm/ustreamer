import os

from setuptools import Extension
from setuptools import setup


# =====
def _find_sources() -> list[str]:
    sources: list[str] = []
    for (root_path, _, names) in os.walk("src"):
        for name in names:
            if name.endswith(".c"):
                sources.append(os.path.join(root_path, name))
    return sources


def _find_flags() -> dict[str, bool]:
    return {
        key[3:]: (value.strip().lower() in ["true", "on", "1"])
        for (key, value) in sorted(os.environ.items())
        if key.startswith("MK_WITH_")
    }


def _make_d_features(flags: dict[str, bool]) -> str:
    features = " ".join([
        f"{key}={int(value)}"
        for (key, value) in flags.items()
    ])
    return f"-DUS_FEATURES=\"{features}\""


def main() -> None:
    flags = _find_flags()
    setup(
        name="ustreamer",
        version="6.31",
        description="uStreamer tools",
        author="Maxim Devaev",
        author_email="mdevaev@gmail.com",
        url="https://github.com/pikvm/ustreamer",
        ext_modules=[
            Extension(
                "ustreamer",
                libraries=["rt", "m", "pthread"],
                extra_compile_args=[
                    "-std=c17", "-D_GNU_SOURCE",
                    _make_d_features(flags),
                ],
                undef_macros=["NDEBUG"],
                sources=_find_sources(),
            ),
        ],
    )


if __name__ == "__main__":
    main()
