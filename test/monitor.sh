#!/bin/bash -Ex
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018, FUJITSU LIMITED. All rights reserved.

rc=77
monitor_pid=65536
logfile=""
conf_file=""
monitor_dimms=""
monitor_regions=""
monitor_namespace=""
smart_supported_bus=""

. $(dirname $0)/common

check_prereq "jq"

trap 'err $LINENO' ERR

check_min_kver "4.15" || do_skip "kernel $KVER may not support monitor service"

start_monitor()
{
	logfile=$(mktemp)
	$NDCTL monitor -l $logfile $1 &
	monitor_pid=$!
	sync; sleep 3
	truncate --size 0 $logfile #remove startup log
}

set_smart_supported_bus()
{
	smart_supported_bus=$NFIT_TEST_BUS0
	monitor_dimms=$($TEST_PATH/list-smart-dimm -b $smart_supported_bus | jq -r .[0].dev)
	if [ -z $monitor_dimms ]; then
		smart_supported_bus=$NFIT_TEST_BUS1
	fi
}

get_monitor_dimm()
{
	jlist=$($TEST_PATH/list-smart-dimm -b $smart_supported_bus $1)
	monitor_dimms=$(jq '.[]."dev"?, ."dev"?' <<<$jlist | sort | uniq | xargs)
	echo $monitor_dimms
}

call_notify()
{
	$TEST_PATH/smart-notify $smart_supported_bus
	sync; sleep 3
}

inject_smart()
{
	$NDCTL inject-smart $monitor_dimms $1
	sync; sleep 3
}

check_result()
{
	jlog=$(cat $logfile)
	notify_dimms=$(jq ."dimm"."dev" <<<$jlog | sort | uniq | xargs)
	[[ $1 == $notify_dimms ]]
}

stop_monitor()
{
	kill $monitor_pid
	rm $logfile
}

test_filter_dimm()
{
	monitor_dimms=$(get_monitor_dimm | awk '{print $1}')
	start_monitor "-d $monitor_dimms"
	call_notify
	check_result "$monitor_dimms"
	stop_monitor
}

test_filter_bus()
{
	monitor_dimms=$(get_monitor_dimm)
	start_monitor "-b $smart_supported_bus"
	call_notify
	check_result "$monitor_dimms"
	stop_monitor
}

test_filter_region()
{
	count=$($NDCTL list -R -b $smart_supported_bus | jq -r .[].dev | wc -l)
	i=0
	while [ $i -lt $count ]; do
		monitor_region=$($NDCTL list -R -b $smart_supported_bus | jq -r .[$i].dev)
		monitor_dimms=$(get_monitor_dimm "-r $monitor_region")
		[ ! -z $monitor_dimms ] && break
		i=$((i + 1))
	done
	start_monitor "-r $monitor_region"
	call_notify
	check_result "$monitor_dimms"
	stop_monitor
}

test_filter_namespace()
{
	reset
	monitor_namespace=$($NDCTL create-namespace -b $smart_supported_bus | jq -r .dev)
	monitor_dimms=$(get_monitor_dimm "-n $monitor_namespace")
	start_monitor "-n $monitor_namespace"
	call_notify
	check_result "$monitor_dimms"
	stop_monitor
	$NDCTL destroy-namespace $monitor_namespace -f
}

test_conf_file()
{
	monitor_dimms=$(get_monitor_dimm)
	conf_file=$(mktemp)
	echo -e "[monitor]\ndimm = $monitor_dimms" > $conf_file
	start_monitor "-c $conf_file"
	call_notify
	check_result "$monitor_dimms"
	stop_monitor
	rm $conf_file
}

test_filter_dimmevent()
{
	monitor_dimms="$(get_monitor_dimm | awk '{print $1}')"

	start_monitor "-d $monitor_dimms -D dimm-unclean-shutdown"
	inject_smart "-U"
	check_result "$monitor_dimms"
	stop_monitor

	if [ $NDCTL_TEST_FAMILY == "PAPR" ]; then
		return
	fi

	inject_value=$($NDCTL list -H -d $monitor_dimms | jq -r .[]."health"."spares_threshold")
	inject_value=$((inject_value - 1))
	start_monitor "-d $monitor_dimms -D dimm-spares-remaining"
	inject_smart "-s $inject_value"
	check_result "$monitor_dimms"
	stop_monitor

	inject_value=$($NDCTL list -H -d $monitor_dimms | jq -r .[]."health"."temperature_threshold")
	inject_value=$((inject_value + 1))
	start_monitor "-d $monitor_dimms -D dimm-media-temperature"
	inject_smart "-m $inject_value"
	check_result "$monitor_dimms"
	stop_monitor
}

do_tests()
{
	test_filter_dimmevent

	if [ $NDCTL_TEST_FAMILY == "PAPR" ]; then
		return
	fi

	test_filter_dimm
	test_filter_bus
	test_filter_region
	test_filter_namespace
	test_conf_file
}

_init
rc=1
reset
set_smart_supported_bus
do_tests
exit 0
