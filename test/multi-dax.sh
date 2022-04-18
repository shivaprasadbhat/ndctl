#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2015-2020 Intel Corporation. All rights reserved.

set -e

rc=77

. $(dirname $0)/common

check_min_kver "4.13" || do_skip "may lack multi-dax support"
check_prereq "jq"

trap 'err $LINENO' ERR

ALIGN_SIZE=`getconf PAGESIZE`

# setup (reset nfit_test dimms)
_init
reset
rc=1

query=". | sort_by(.available_size) | reverse | .[0].dev"
region=$($NDCTL list -b $NFIT_TEST_BUS0 -t pmem -Ri | jq -r "$query")

json=$($NDCTL create-namespace -b $NFIT_TEST_BUS0 -r $region -t pmem -m devdax -a $ALIGN_SIZE -s 16M)
chardev1=$(echo $json | jq ". | select(.mode == \"devdax\") | .daxregion.devices[0].chardev")
json=$($NDCTL create-namespace -b $NFIT_TEST_BUS0 -r $region -t pmem -m devdax -a $ALIGN_SIZE -s 16M)
chardev2=$(echo $json | jq ". | select(.mode == \"devdax\") | .daxregion.devices[0].chardev")

exit 0
