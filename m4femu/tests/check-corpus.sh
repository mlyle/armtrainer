#!/bin/sh

set -eu

runner=$1
object=$2

count=0
arm-none-eabi-objdump -d -Mforce-thumb "$object" |
	awk '/^[[:space:]]*[0-9a-f]+:/ {
		if (length($2) == 4 && length($3) == 4) print $2 $3
		else if (length($2) == 4) print $2
	}' |
	while read -r instruction; do
		result=$($runner --probe "$instruction" || true)
		case "$result" in
			(undefined|unpredictable|fetch\ fault)
				echo "corpus instruction $instruction: $result" >&2
				exit 1
				;;
		esac
		count=$((count + 1))
	done

echo "m4f: assembled Cortex-M4F encoding corpus decoded"
