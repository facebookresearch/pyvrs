# What is pyvrs?

pyvrs is a Python interface for C++ library [VRS](https://github.com/facebookresearch/vrs) using [pybind11](https://github.com/pybind/pybind11).

# Documentation

See [API documentation](https://pyvrs.readthedocs.io/en/latest/)

# Installation
## Install released builds
pypi package is built with [this Github Action](https://github.com/facebookresearch/pyvrs/blob/main/.github/workflows/deploy.yml) manually.
```
pip install vrs
```

## From source
```
# Build locally
git clone --recursive https://github.com/facebookresearch/pyvrs.git
cd pyvrs
# if you are updating an existing checkout
git submodule sync --recursive
git submodule update --init --recursive

# Install VRS dependencies: https://github.com/facebookresearch/vrs#instructions-macos-and-ubuntu-and-container

python -m pip install -e .
```

# Contributing

We welcome contributions! See [CONTRIBUTING](CONTRIBUTING.md) for details on how
to get started, and our [code of conduct](CODE_OF_CONDUCT.md).

# License

VRS is released under the [Apache 2.0 license](LICENSE).
