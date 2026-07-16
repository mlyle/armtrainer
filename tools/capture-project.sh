#!/bin/sh

set -e

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
project_dir="$root_dir/examples/student-projects"
disassembler="$root_dir/build/tools/disasmthumb"

mkdir -p "$project_dir"

printf 'Project name? '
read -r projname

case "$projname" in
   ("")       echo "empty" ; exit 1 ;;
   (*[!a-z0-9_-]*) echo "contains an invalid character" ; exit 1 ;;
   (*)        echo "Gathering projects for $projname" ;;
esac

echo "Will do it"
dfu-util -a 0 -i 0 -s 0x08004000:0x4000 -U "$project_dir/$projname-1.bin"
dfu-util -a 0 -i 0 -s 0x08008000:0x4000 -U "$project_dir/$projname-2.bin"
dfu-util -a 0 -i 0 -s 0x0800C000:0x4000 -U "$project_dir/$projname-3.bin"

make -C "$root_dir" flash build/tools/disasmthumb

"$disassembler" < "$project_dir/$projname-1.bin" > "$project_dir/$projname-1.lst"

head -20 < "$project_dir/$projname-1.lst"

echo "print?"

read print

case "$print" in
	([Yy]) enscript -p - "$project_dir/$projname-1.lst" |
		ps2pdf - - |
		lpr -P "${PRINTER:-_192_168_1_233}" -# "${COPIES:-2}" ;;
esac
