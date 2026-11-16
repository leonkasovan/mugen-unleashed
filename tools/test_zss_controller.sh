#!/bin/sh
# Compile the real ZSS/INI/controller/expression bridge in the CNS host harness.
set -eu
cd "$(dirname "$0")/.."
export TEST_ZSS_CONTROLLER=1
export OUT="${OUT:-$PWD/build/tmp/zss-controller}"
exec sh tools/test_cns_abi.sh
