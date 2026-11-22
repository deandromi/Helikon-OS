#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fuzz the parsers.
#
#   ./tools/fuzz.sh                    # every harness, 60 seconds each
#   ./tools/fuzz.sh 20                 # every harness, 20 seconds each
#   ./tools/fuzz.sh id3v2 300          # one harness for five minutes
#   ./tools/fuzz.sh --replay           # run the committed corpus, no libFuzzer
#   ./tools/fuzz.sh --list             # print the harnesses and exit
#
# `--replay` needs no fuzzer runtime: it builds each harness against
# `tools/fuzz-replay-main.c` with the address and undefined-behaviour sanitizers
# and runs every file in its corpus through it. That is the mode a Mac with only
# the Command Line Tools can run — Xcode's clang ships no `libclang_rt.fuzzer`,
# so the guided mode is a Linux/CI thing. A finding from either mode becomes a
# file in the corpus and stays a test.
#
# Each harness is a libFuzzer target: clang's `-fsanitize=fuzzer` plus the
# address and undefined-behaviour sanitizers, so a read past a buffer or an
# undefined shift is a crash with a reproducer file rather than a quiet wrong
# answer. The harnesses and their sources are listed below; each one is a single
# file under its suite's `fuzz/` directory.
#
# A finding lands in `tests/<suite>/fuzz/findings/` as a file whose name is the
# crash kind and a hash. Copy it into `tests/<suite>/fuzz/corpus/<harness>/` and
# commit both the file and the fix: the corpus is the regression suite, and the
# CI smoke run (`./tools/fuzz.sh 20`) replays it every push.
#
# clang only. Nothing here touches a device, and every input is generated in
# memory from the fuzzer's bytes.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

if ! command -v clang >/dev/null 2>&1; then
    echo "fuzz: clang is required" >&2
    exit 2
fi

HARNESSES="id3v2 vorbis hdb manifest journal playlist"

# One line per harness: suite, sources, include directories, and the invariant
# in one sentence (printed with the run, because a fuzz target without a stated
# invariant tends to only prove that the code does not crash).
sources_for() {
    case "$1" in
    id3v2) echo "firmware/codec/tag/id3v2.c firmware/lib/mem.c" ;;
    vorbis) echo "firmware/codec/tag/vorbis.c firmware/lib/mem.c" ;;
    hdb) echo "firmware/db/hdb.c firmware/lib/crc32.c firmware/lib/mem.c \
firmware/lib/string.c" ;;
    manifest) echo "bootloader/manifest.c bootloader/bootstate.c \
bootloader/ed25519.c bootloader/keys.c firmware/lib/mem.c \
firmware/lib/sha256.c" ;;
    journal) echo "firmware/hfs/hfs_journal.c firmware/lib/crc32.c \
firmware/lib/mem.c firmware/lib/string.c" ;;
    playlist) echo "firmware/player/playlist.c firmware/lib/mem.c" ;;
    esac
}

includes_for() {
    case "$1" in
    id3v2 | vorbis) echo "-Ifirmware/codec -Ifirmware/codec/tag -Ifirmware/lib" ;;
    hdb) echo "-Ifirmware/db -Ifirmware/lib" ;;
    manifest) echo "-Ibootloader -Ifirmware/lib -Ifirmware/codec" ;;
    journal) echo "-Ifirmware/hfs -Ifirmware/lib -Itests/hfsplus" ;;
    playlist) echo "-Ifirmware/player -Ifirmware/lib" ;;
    esac
}

suite_for() {
    case "$1" in
    id3v2 | vorbis) echo codec ;;
    hdb) echo musicdb ;;
    manifest) echo bootloader ;;
    journal) echo hfsplus ;;
    playlist) echo player ;;
    esac
}

invariant_for() {
    case "$1" in
    id3v2) echo "never reads past the buffer, and every returned field is inside it" ;;
    vorbis) echo "never reads past the buffer, and comments stay terminated" ;;
    hdb) echo "every offset it reports is inside the file it was given" ;;
    manifest) echo "a rejected manifest or bootstate never reports success" ;;
    journal) echo "a journal whose checksum does not match is never reported clean" ;;
    playlist) echo "every entry path is absolute, terminated and free of control characters" ;;
    esac
}

replay=no
case "${1:-}" in
--list)
    echo "$HARNESSES"
    exit 0
    ;;
--replay)
    replay=yes
    shift
    ;;
esac

if [ "$replay" = yes ]; then
    seconds=0
    only=${1:-}
else
    # The runtime is a separate archive from the compiler; Ubuntu's clang has it
    # and Xcode's does not, so say which one is missing instead of failing at the
    # link with a path.
    if ! echo 'int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long s){(void)d;(void)s;return 0;}' |
        clang -x c - -fsanitize=fuzzer -o /dev/null >/dev/null 2>&1; then
        echo "fuzz: this clang has no libFuzzer runtime (libclang_rt.fuzzer)." >&2
        echo "fuzz: install llvm (brew install llvm) or run --replay for the" \
            "committed corpus." >&2
        exit 2
    fi
    seconds=${1:-60}
    only=${2:-}
    case "$seconds" in
    '' | *[!0-9]*)
        echo "fuzz: seconds must be a number" >&2
        exit 2
        ;;
    esac
fi

# The guided mode needs the fuzzer runtime; the replay mode is the same harness
# built as an ordinary program, so it runs anywhere the sanitizers do.
if [ "$replay" = yes ]; then
    CXXFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
-fno-sanitize-recover=undefined"
else
    CXXFLAGS="-O1 -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
-fno-sanitize-recover=undefined"
fi
status=0
ran=0

for harness in $HARNESSES; do
    if [ -n "$only" ] && [ "$only" != "$harness" ]; then
        continue
    fi
    # shellcheck disable=SC2046
    suite=$(suite_for "$harness")
    dir="tests/$suite/fuzz"
    corpus="$dir/corpus/$harness"
    findings="$dir/findings"
    bin="$ROOT/build/fuzz/$harness"
    mkdir -p "$corpus" "$findings" "$(dirname "$bin")"

    printf '== %s: %s\n' "$harness" "$(invariant_for "$harness")"
    driver="$dir/fuzz_$harness.c"
    [ "$replay" = yes ] && driver="$driver $ROOT/tools/fuzz-replay-main.c"
    # shellcheck disable=SC2046,SC2086
    if ! clang $CXXFLAGS $(includes_for "$harness") -I"$dir" \
        $driver $(sources_for "$harness") -o "$bin"; then
        echo "fuzz: $harness did not build" >&2
        status=1
        continue
    fi

    ran=$((ran + 1))
    if [ "$replay" = yes ]; then
        # shellcheck disable=SC2046
        if ! "$bin" $(find "$corpus" -type f | sort) >"$ROOT/build/fuzz/$harness.log" 2>&1; then
            printf '  FAIL  %s: see %s\n' "$harness" "$ROOT/build/fuzz/$harness.log"
            tail -5 "$ROOT/build/fuzz/$harness.log" | sed 's/^/        /'
            status=1
        else
            printf '  ok    %s  %s file(s)\n' "$harness" \
                "$(find "$corpus" -type f | wc -l | tr -d ' ')"
        fi
        continue
    fi

    # -timeout kills a single input that hangs; a parser that loops forever is a
    # bug worth a file just like a crash. The corpus is the committed one: a
    # mutation that finds something new is saved there and survives the run.
    if ! "$bin" \
        -max_total_time="$seconds" \
        -timeout=5 \
        -rss_limit_mb=2048 \
        -max_len=8192 \
        -artifact_prefix="$findings/" \
        -print_final_stats=1 \
        "$corpus" >"$ROOT/build/fuzz/$harness.log" 2>&1; then
        printf '  FAIL  %s: see %s\n' "$harness" "$ROOT/build/fuzz/$harness.log"
        tail -5 "$ROOT/build/fuzz/$harness.log" | sed 's/^/        /'
        status=1
        continue
    fi
    # Invariant violations abort the process, so reaching here means the run
    # itself completed; the coverage line says whether the harness actually got
    # into the parser.
    printf '  ok    %s  %s\n' "$harness" \
        "$(grep -o 'cov: [0-9]* ft: [0-9]*' "$ROOT/build/fuzz/$harness.log" |
            tail -1)"
done

if [ "$ran" = 0 ]; then
    echo "fuzz: no harness matched '${only}'" >&2
    exit 2
fi

if [ "$status" != 0 ]; then
    echo "fuzz: at least one harness failed; a reproducer is in" \
        "tests/<suite>/fuzz/findings/" >&2
    exit 1
fi

echo "fuzz: $ran harness(es) clean"
