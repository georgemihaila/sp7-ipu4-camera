#!/bin/sh
# Source this helper to use kernel_tree_release, or pass a tree to print it.

kernel_tree_release() {
	_tree=$1
	if [ -f "$_tree/include/generated/utsrelease.h" ]; then
		sed -n 's/^#define UTS_RELEASE "\(.*\)"$/\1/p' \
			"$_tree/include/generated/utsrelease.h"
	elif [ -f "$_tree/include/config/kernel.release" ]; then
		cat "$_tree/include/config/kernel.release"
	fi
}

if [ "$#" -gt 0 ]; then
	[ "$#" -eq 1 ] || {
		printf 'usage: %s KERNEL_BUILD_TREE\n' "$0" >&2
		exit 2
	}
	kernel_tree_release "$1"
fi
