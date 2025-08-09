from setuptools import setup, find_packages
from setuptools.dist import Distribution

class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        # Force platform-specific wheel (we bundle a native shared library)
        return True

setup(
    name="tacozip",
    version="0.1.0",
    description="TACO ZIP: CIP64 (always ZIP64) writer with a fixed 64-byte ghost LFH",
    author="Cesar",
    package_dir={"": "."},
    packages=find_packages(where="."),
    include_package_data=True,
    package_data={"tacozip": ["libtacozip.*"]},  # .so/.dylib/.dll
    python_requires=">=3.9",
    zip_safe=False,
    distclass=BinaryDistribution,
)
