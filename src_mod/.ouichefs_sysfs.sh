#!bin/bash

function ouichefs() {
	echo "$@" > /sys/kernel/ouichefs_sysfs
}
