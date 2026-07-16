#!/bin/sh

set -e

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
project_dir="$root_dir/examples/student-projects"
disassembler="$root_dir/build/tools/disasmthumb"

if [ ! -x "$disassembler" ]; then
	make -C "$root_dir" build/tools/disasmthumb
fi

printf 'Project name? '
read -r projname

case "$projname" in
	("") echo "empty project name" >&2; exit 1 ;;
	(*[!a-zA-Z0-9_-]*) echo "invalid project name" >&2; exit 1 ;;
esac

"$disassembler" < "$project_dir/$projname.bin" > "$project_dir/$projname.lst"
head -20 < "$project_dir/$projname.lst"

printf 'Print? [y/N] '
read -r print

case "$print" in
	([Yy]) enscript -p - "$project_dir/$projname.lst" |
		ps2pdf - - |
		lpr -P "${PRINTER:-_192_168_1_233}" -# "${COPIES:-2}" ;;
esac
