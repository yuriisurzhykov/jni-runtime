#!/usr/bin/env bash
# Checks a built native library against the release rules of roadmap P8 and docs/build-and-delivery.md:
#
#   1. the only symbol the library exports is JNI_OnLoad (JNI_OnUnload is allowed too);
#   2. it needs at most one C++ runtime, libc++_shared.so, and no libstdc++;
#   3. every LOAD segment is aligned to at least 16 KB (Android 15+ devices with 16 KB pages);
#   4. the file still has its symbol table and debug sections, so a tombstone can be symbolicated (--require-symbols).
#
# Usage: check-native-lib.sh [--min-align BYTES] [--require-symbols] [--allow-libstdcxx] <lib.so>
#   --allow-libstdcxx  for host libraries only: a Linux host links libstdc++, Android never does.
# Environment: READELF and NM name the tools; for an NDK build use llvm-readelf and llvm-nm from the NDK.
# Exit code: 0 all rules hold, 1 a rule is broken (every broken rule is printed), 2 wrong usage or a tool that could not
# inspect the file (never reported as OK).

set -u

READELF=${READELF:-readelf}
NM=${NM:-nm}
min_align=16384
require_symbols=0
allow_libstdcxx=0
lib=""

while [ $# -gt 0 ]; do
    case "$1" in
        --min-align) min_align=$2; shift 2 ;;
        --require-symbols) require_symbols=1; shift ;;
        --allow-libstdcxx) allow_libstdcxx=1; shift ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) lib=$1; shift ;;
    esac
done

if [ -z "$lib" ] || [ ! -f "$lib" ]; then
    echo "usage: $0 [--min-align BYTES] [--require-symbols] [--allow-libstdcxx] <lib.so>" >&2
    exit 2
fi

failed=0
fail() { echo "FAIL: $lib: $*" >&2; failed=1; }

# Run each inspection tool once and stop if it cannot do its job: a check that could not look must not say OK.
inspect_failed() { echo "ERROR: $lib: cannot inspect the library: $*" >&2; exit 2; }
nm_out=$("$NM" -D --defined-only "$lib") || inspect_failed "'$NM -D' failed"
dynamic_out=$("$READELF" -d "$lib") || inspect_failed "'$READELF -d' failed"
segments_out=$("$READELF" -lW "$lib") || inspect_failed "'$READELF -lW' failed"
sections_out=$("$READELF" -SW "$lib") || inspect_failed "'$READELF -SW' failed"

# 1. Exported symbols: defined and visible to the dynamic linker.
exported=$(echo "$nm_out" | awk '{print $NF}' | sort -u)
for symbol in $exported; do
    case "$symbol" in
        JNI_OnLoad|JNI_OnUnload) ;;
        *) fail "exports '$symbol' (only JNI_OnLoad may be exported)" ;;
    esac
done
case " $(echo $exported) " in
    *" JNI_OnLoad "*) ;;
    *) fail "does not export JNI_OnLoad" ;;
esac

# 2. C++ runtime in the NEEDED list.
needed=$(echo "$dynamic_out" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
for dep in $needed; do
    case "$dep" in
        libstdc++*) [ "$allow_libstdcxx" -eq 1 ] || fail "needs $dep (Android has no libstdc++; use libc++_shared)" ;;
    esac
done
count=$(echo "$needed" | grep -c '^libc++_shared\.so$' || true)
[ "$count" -le 1 ] || fail "lists libc++_shared.so $count times"

# 3. Alignment of the LOAD segments (the last column of 'Align' in readelf -lW is hexadecimal).
aligns=$(echo "$segments_out" | awk '$1 == "LOAD" {print $NF}')
[ -n "$aligns" ] || inspect_failed "no LOAD segment found; is this an ELF shared library?"
for align in $aligns; do
    if [ $((align)) -lt "$min_align" ]; then
        fail "LOAD segment aligned to $((align)) bytes, need at least $min_align"
    fi
done

# 4. Symbolication: the symbol table, and the DWARF sections that give file and line in a tombstone.
if [ "$require_symbols" -eq 1 ]; then
    for section in .symtab .debug_info .debug_line; do
        echo "$sections_out" | grep -q "[] ]$section " ||
            fail "has no $section (stripped); keep the unstripped file for tombstones"
    done
fi

[ "$failed" -eq 0 ] && echo "OK: $lib"
exit "$failed"
