from setuptools import setup, find_packages
from setuptools.dist import Distribution
from pathlib import Path

README = (Path(__file__).parent.parent / "README.md")
long_desc = README.read_text(encoding="utf-8") if README.exists() else ""

class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        return True

setup(
    name="tacozip",
    version="0.1.0",
    description="TACO ZIP: CIP64 (always ZIP64) writer with a fixed 64-byte ghost LFH",
    long_description=long_desc,
    long_description_content_type="text/markdown",
    author="Cesar Aybar",
    author_email="cesar.aybar@proton.me",
    packages=find_packages(where="."),
    package_dir={"": "."},
    include_package_data=True,
    # empaqueta cualquier lib nativa que copiaste a python/tacozip/
    package_data={"tacozip": ["libtacozip.*"]},
    python_requires=">=3.9",
    zip_safe=False,
    distclass=BinaryDistribution,
)
