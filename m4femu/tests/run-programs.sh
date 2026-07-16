#!/bin/sh

set -eu

runner=$1
program_dir=$2

for name in integer memory floating; do
	expected=$("$program_dir/$name-reference")
	actual=$("$runner" "$program_dir/$name.bin")
	if [ "$actual" != "$expected" ]; then
		echo "$name: compiled program result mismatch" >&2
		echo "reference: $expected" >&2
		echo "emulator:  $actual" >&2
		exit 1
	fi
	echo "m4f: freestanding $name C program passed"
done
