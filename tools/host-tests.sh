#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Host test runner.
#
#   ./tools/host-tests.sh                    # portable suites, default cc
#   ./tools/host-tests.sh clang              # the same, built with clang
#   ./tools/host-tests.sh clang --sanitize   # ASan + UBSan over the same code
#   ./tools/host-tests.sh --macos            # the disk-image suites (Darwin)
#   ./tools/host-tests.sh clang --macos --sanitize   # both, in that order
#   ./tools/host-tests.sh --keep-going       # run them all, report every failure
#   ./tools/host-tests.sh --fast             # keep the objects from the last run
#   ./tools/host-tests.sh --list             # print the portable suites, exit
#
# This is the single command a change is expected to pass before it is reviewed,
# and the gate the preview release runs on a runner. Each suite keeps its own log
# under `build/host-tests/`; the run stops at the first red suite unless
# `--keep-going` is given, and prints just the failing suite and its log.
#
# Two groups, because they need different things:
#
#   portable   plain C on any host: hfsplus firmware bootloader vfs codec
#              musicdb player ui usb, plus library when HOST_TESTS_IMAGE names
#              an HFS+ image. The `ui` suite needs Pillow to write its PNGs.
#   --macos    tests/image: the disposable disk images, hdiutil, fsck_hfs and
#              the macOS mount. It creates its own images and never touches a
#              device or a library. `HOST_TESTS_MACOS_SKIP=format-test` leaves a
#              driver out, for a host whose disk cannot hold the 140 GiB sparse
#              image `format-test` formats — a hosted runner, typically; the
#              skip is printed, never silent.
#
# `--sanitize` uses the suites' own `sanitize` targets (AddressSanitizer plus
# UndefinedBehaviorSanitizer) and skips the ones that do not offer them. It needs
# a compiler with the sanitizers, which is why it is a separate pass.
#
# Every suite is cleaned before it is built unless `--fast` is given. A
# `sanitize` target leaves instrumented objects behind and a later plain build
# will then fail to link them (`tests/firmware` did exactly that), so the default
# is the same clean build a runner gets. `--fast` is for the tight loop while
# editing one suite.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOGS="$ROOT/build/host-tests"

# Suites that build and run on any host with a C compiler.
SUITES="hfsplus firmware bootloader vfs codec musicdb player ui usb"
# Of those, the ones with a `sanitize` target.
SANITIZE_SUITES="hfsplus firmware bootloader vfs codec musicdb player usb"
# The macOS disk-image suites, in one `make` target list.
MACOS_TARGETS="test crash-test format-test loader-test write-diag-test \
outer-diag-test ops-test unicode-order-test library-test"

cc=cc
sanitize=no
macos=no
clean=yes
keep_going=no

for arg in "$@"; do
    case "$arg" in
        --sanitize) sanitize=yes ;;
        --macos) macos=yes ;;
        --clean) clean=yes ;;
        --fast) clean=no ;;
        --keep-going) keep_going=yes ;;
        --list)
            echo "$SUITES"
            exit 0
            ;;
        -*)
            echo "host-tests: unknown option $arg" >&2
            exit 2
            ;;
        *)
            cc=$arg
            ;;
    esac
done

failed=""
passed=0
skipped=""

# One suite through its own Makefile. `$1` suite, `$2` goal, `$3` extra make
# arguments, `$4` log name.
run_suite() {
    suite=$1
    goal=$2
    extra=$3
    log="$LOGS/$mode/$4.log"
    dir="$ROOT/tests/$suite"
    if [ ! -d "$dir" ]; then
        skipped="$skipped $4"
        return 0
    fi
    start=$(date +%s)
    if [ "$clean" = yes ]; then
        make -C "$dir" clean >/dev/null 2>&1 || true
    fi
    # Unquoted on purpose: a suite may name several goals, and both `$goal` and
    # `$extra` are word lists.
    # shellcheck disable=SC2086
    if make -C "$dir" $goal CC="$cc" HOSTCC="$cc" $extra >"$log" 2>&1; then
        seconds=$(( $(date +%s) - start ))
        printf '  ok    %-12s %3ss\n' "$4" "$seconds"
        passed=$((passed + 1))
        return 0
    fi
    printf '  FAIL  %-12s      %s\n' "$4" "$log"
    tail -20 "$log" | sed 's/^/        /'
    failed="$failed $4"
    return 1
}

run_loop() {
    for suite in $suites; do
        if ! run_suite "$suite" "$goal" "$extra" "$suite"; then
            [ "$keep_going" = yes ] || return 1
        fi
    done
    return 0
}

if [ "$sanitize" = yes ]; then
    suites=$SANITIZE_SUITES
    mode=sanitize
    goal=sanitize
    extra=""
else
    suites=$SUITES
    mode=plain
    goal="test"
    extra=""
fi
# `library` takes the image through a variable, so it runs on its own after the
# loop rather than inside it.
mkdir -p "$LOGS/$mode"
printf 'host-tests: %s, mode %s\n' "$cc" "$mode"

status=0
run_loop || status=1

if [ "$sanitize" = no ] && [ -n "${HOST_TESTS_IMAGE:-}" ]; then
    mode=plain
    mkdir -p "$LOGS/$mode"
    printf 'host-tests: library, image %s\n' "$HOST_TESTS_IMAGE"
    if ! run_suite library test "IMAGE=$HOST_TESTS_IMAGE" library; then
        status=1
    fi
fi

if [ "$macos" = yes ]; then
    if [ "$(uname -s)" = Darwin ]; then
        mode=macos
        mkdir -p "$LOGS/$mode"
        targets=""
        for target in $MACOS_TARGETS; do
            case " ${HOST_TESTS_MACOS_SKIP:-} " in
                *" $target "*) printf 'host-tests: skipping %s\n' "$target" ;;
                *) targets="$targets $target" ;;
            esac
        done
        printf 'host-tests: disk-image suites (macOS)\n'
        # shellcheck disable=SC2086
        if ! run_suite image "$targets" "" image; then
            status=1
        fi
    else
        printf 'host-tests: --macos needs Darwin; skipped\n'
    fi
fi

if [ -n "$failed" ]; then
    printf 'host-tests: %s suite(s) failed:%s\n' \
        "$(echo "$failed" | wc -w | tr -d ' ')" "$failed" >&2
    exit 1
fi

printf 'host-tests: %s suite(s) passed\n' "$passed"
[ -n "$skipped" ] && printf 'host-tests: not present:%s\n' "$skipped"
exit "$status"
