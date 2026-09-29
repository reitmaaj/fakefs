#!/bin/sh -eu

set -eu

cd "$(dirname "$0")/.."

printf '== test_property_model ==\n'
python3 test/tools/test_property_model.py

printf '== test_differential_posix ==\n'
python3 test/tools/test_differential_posix.py

printf '== test_differential_tmpfs ==\n'
python3 test/tools/test_differential_tmpfs.py

printf '== test_differential_massive ==\n'
python3 test/tools/test_differential_massive.py
