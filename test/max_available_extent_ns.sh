#!/bin/bash -Ex
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018, FUJITSU LIMITED. All rights reserved.

rc=77

. $(dirname $0)/common

trap 'err $LINENO' ERR

check_min_kver "4.19" || do_skip "kernel $KVER may not support max_available_size"
check_prereq "jq"

do_test()
{
	region=$($NDCTL list -b $NFIT_TEST_BUS0 -R -t pmem | jq -r 'sort_by(-.size) | .[].dev' | head -1)

	available_sz=$($NDCTL list -r $region | jq -r .[].available_size)
	size=$(( available_sz/4 ))

	NS=()
	for ((i=0; i<3; i++))
	do
		NS[$i]=$($NDCTL create-namespace -r $region -t pmem -s $size | jq -r .dev)
		[[ -n ${NS[$i]} ]]
	done

	$NDCTL disable-namespace ${NS[1]}
	$NDCTL destroy-namespace ${NS[1]}

	$NDCTL create-namespace -r $region -t pmem
}

_init
rc=1
reset
do_test
exit 0
