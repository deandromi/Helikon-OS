#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Differential test against macOS for HFS+ ordering and for reading a volume
# that macOS wrote.
#
# Apple's FastUnicodeCompare cannot be checked against a unit test: the unit test
# would be our own understanding of it again. What can be checked is the result
# macOS produces, because the catalog of a directory is a B-tree whose key order
# *is* that comparator. So: macOS creates a corpus, macOS builds the tree, and
# then our reader lists it back and our comparator re-sorts it. If the two orders
# agree, our comparator agrees with Apple's on every pair in the corpus.
#
# The same image also carries a small tree whose sizes and contents macOS wrote,
# which is the read half of the differential: names, kinds, sizes and one file's
# SHA-256 have to come back the way macOS left them.
#
#   make -C tests/image unicode-order-test
#
# macOS only (hdiutil, fsck_hfs). It creates its own image and never accepts an
# existing one, a mounted volume or a device.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: the macOS differential needs hdiutil and fsck_hfs"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-unicode-order.XXXXXX")
IMAGE="$TMP/helikon-order.dmg"
MOUNT="$TMP/mnt"
DEV=""
MOUNTED=""
TOOL="$HERE/unicode_order_host"

cleanup() {
    if [ -n "$MOUNTED" ]; then
        hdiutil detach "$MOUNT" >/dev/null 2>&1 || true
    fi
    if [ -n "$DEV" ]; then
        hdiutil detach "$DEV" >/dev/null 2>&1 || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

fsck_image() {
    DEV=$(hdiutil attach -nomount -nobrowse "$1" 2>"$TMP/attach.log" |
          awk 'NR == 1 { print $1 }')
    [ -n "$DEV" ] || {
        cat "$TMP/attach.log" >&2 || true
        fail "could not attach $1"
    }
    if ! OUT=$(fsck_hfs -fn "$DEV" 2>&1); then
        echo "$OUT" >&2
        hdiutil detach "$DEV" >/dev/null 2>&1 || true
        DEV=""
        fail "fsck_hfs reported an error"
    fi
    echo "$OUT"
    echo "$OUT" | grep -q "appears to be OK" ||
        fail "fsck_hfs did not report a clean volume"
    if echo "$OUT" | grep -qE '\*\* (Repairing|Rebuilding|Fixing)'; then
        fail "fsck_hfs attempted a repair"
    fi
    hdiutil detach "$DEV" >/dev/null 2>&1
    DEV=""
}

make -C "$HERE" unicode_order_host >/dev/null ||
    fail "could not build unicode_order_host"

if ! hdiutil create -size 24m -fs HFS+J -volname HelikonOrder -layout NONE \
        "$IMAGE" >"$TMP/create.log" 2>&1; then
    cat "$TMP/create.log" >&2
    fail "could not create the disposable HFS+ image"
fi
echo "created a disposable 24 MiB journaled HFS+ image"

mkdir -p "$MOUNT"
if ! hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
        >"$TMP/mount.log" 2>&1; then
    cat "$TMP/mount.log" >&2
    fail "could not mount the disposable image"
fi
MOUNTED=1

# The corpus, and the corpus macOS actually produced.
python3 - "$MOUNT" "$TMP" <<'PY'
import hashlib
import os
import random
import sys

mount, tmp = sys.argv[1], sys.argv[2]

# Names chosen for what they exercise, not for looks: digit order (HFS+ compares
# digits as digits), case folding, punctuation, precomposed Latin, decomposed
# combining marks, marks that need canonical reordering, Greek, Cyrillic, Hebrew,
# Arabic, Devanagari, CJK, Hangul, kana, emoji, fullwidth forms, and the 255-unit
# name limit at both sides of the boundary.
names = [
    "1", "02", "9", "10", "1234567890",
    "Aardvark", "aardvark two", "Apple", "apple pie", "Apricot",
    "banana", "Banana split", "cherry", "Zebra", "zebra crossing",
    "_underscore", "dot.name", "bracket[1]", "Mixed CASE name",
    "Cafés", "Naïve", "Ålesund", "Ünïcödé", "ñandú", "ßeta", "Æther",
    "Œuvre", "İstanbul", "x\u0308\u0301y", "q\u0323\u0307",
    "αβγ", "Ωmega", "Привет", "שלום", "مرحبا", "नमस्ते",
    "中文", "日本語", "漢字", "한글", "ひらがな", "カタカナ",
    "🎵", "🎶 music", "ＡＢＣ",
    "a" * 255, "a" * 254 + "b",
]

random.Random(20261122).shuffle(names)

order = os.path.join(mount, "Order")
tree = os.path.join(mount, "Tree")
os.mkdir(order)
os.mkdir(tree)

for name in names:
    path = os.path.join(order, name)
    with open(path, "wb"):
        pass

with open(os.path.join(tmp, "names.txt"), "w", encoding="utf-8") as out:
    for name in names:
        out.write(name + "\n")

# A tree with the shapes a reader has to get right: zero length, one byte, a
# whole block, a partial block, a file that needs many blocks, and nesting.
files = {
    "Tree/empty.txt": b"",
    "Tree/one.bin": b"\x00",
    "Tree/block.bin": bytes((index * 31 + 7) & 0xFF for index in range(4096)),
    "Tree/tenk.bin": bytes((index * 17 + 3) & 0xFF for index in range(10000)),
    "Tree/sub/nested.bin": bytes((index * 13 + 5) & 0xFF for index in range(70000)),
    "Tree/sub/deeper/leaf.txt": b"leaf\n",
}
os.makedirs(os.path.join(tree, "sub", "deeper"))
for relative, data in files.items():
    with open(os.path.join(mount, relative), "wb") as out:
        out.write(data)

expected = []
for directory, dirnames, filenames in os.walk(mount):
    relative = os.path.relpath(directory, mount)
    if relative == "." or not relative.startswith("Tree"):
        continue
    for dirname in dirnames:
        path = os.path.join(relative, dirname).replace(os.sep, "/")
        if path.startswith("Tree"):
            expected.append((path, "folder", "-"))
    for filename in filenames:
        path = os.path.join(relative, filename).replace(os.sep, "/")
        if not path.startswith("Tree"):
            continue
        size = os.path.getsize(os.path.join(directory, filename))
        expected.append((path, "file", size))

# os.walk puts "sub/deeper" under "sub"; the tool prints the same relative paths.
with open(os.path.join(tmp, "tree.txt"), "w", encoding="utf-8") as out:
    for path, kind, size in sorted(expected):
        out.write(f"{path}\t{kind}\t{size}\n")

digest = hashlib.sha256(files["Tree/sub/nested.bin"]).hexdigest()
with open(os.path.join(tmp, "read.txt"), "w", encoding="utf-8") as out:
    out.write("Tree/sub/nested.bin " + digest + "\n")

print(f"macOS created {len(names)} names and {len(files)} files")
PY

# macOS has to see exactly what was asked for; otherwise the comparison below is
# against something else.
python3 - "$MOUNT" "$TMP/tree.txt" <<'PY'
import os
import sys
import unicodedata

mount, expected_path = sys.argv[1], sys.argv[2]
actual = []
for directory, dirnames, filenames in os.walk(mount):
    relative = os.path.relpath(directory, mount)
    if relative == "." or not relative.startswith("Tree"):
        continue
    for dirname in dirnames:
        path = os.path.join(relative, dirname).replace(os.sep, "/")
        if path.startswith("Tree"):
            actual.append((path, "folder", "-"))
    for filename in filenames:
        path = os.path.join(relative, filename).replace(os.sep, "/")
        if not path.startswith("Tree"):
            continue
        size = os.path.getsize(os.path.join(directory, filename))
        actual.append((path, "file", size))
actual.sort()

expected = []
with open(expected_path, encoding="utf-8") as source:
    for line in source:
        path, kind, size = line.rstrip("\n").split("\t")
        expected.append((path, kind, "-" if size == "-" else int(size)))

if actual != expected:
    raise SystemExit(f"macOS tree mismatch: {actual!r} != {expected!r}")
print("macOS sees the tree it created")
PY

hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""
fsck_image "$IMAGE"

# 1. The order macOS built, read back through our catalog iterator.
if ! "$TOOL" list "$IMAGE" /Order > "$TMP/catalog-order.txt" 2>"$TMP/list.log"; then
    cat "$TMP/list.log" >&2
    fail "could not list the corpus"
fi
COUNT=$(wc -l < "$TMP/catalog-order.txt" | tr -d ' ')
[ "$COUNT" -ge 40 ] || fail "listed $COUNT names, expected the whole corpus"

# 2. The same names in the order our comparator predicts. Agreeing on every
#    pair is what "matches Apple's FastUnicodeCompare" has to mean.
"$TOOL" sort < "$TMP/catalog-order.txt" > "$TMP/our-order.txt"
if ! cmp -s "$TMP/catalog-order.txt" "$TMP/our-order.txt"; then
    echo "catalog order (macOS) vs ours:" >&2
    paste "$TMP/catalog-order.txt" "$TMP/our-order.txt" | awk -F'\t' '$1 != $2' | head -20 >&2
    fail "our comparator orders the corpus differently from macOS"
fi
echo "our comparator matches the catalog order macOS built for $COUNT names"

# 3. Canonical form: the names we asked for, normalized by our code, have to be
#    the set macOS stored.
"$TOOL" canon < "$TMP/names.txt" | LC_ALL=C sort > "$TMP/expected-set.txt"
LC_ALL=C sort "$TMP/catalog-order.txt" > "$TMP/actual-set.txt"
if ! cmp -s "$TMP/expected-set.txt" "$TMP/actual-set.txt"; then
    echo "names macOS stored vs our canonical form:" >&2
    diff -u "$TMP/expected-set.txt" "$TMP/actual-set.txt" | head -40 >&2 || true
    fail "canonical names differ from what macOS stored"
fi
echo "canonical form matches macOS for $COUNT names"

# 4. The read differential: kinds, sizes and paths come back as macOS left them.
"$TOOL" tree "$IMAGE" /Tree > "$TMP/our-tree.txt" 2>"$TMP/tree.log" || {
    cat "$TMP/tree.log" >&2
    fail "could not walk the tree"
}
LC_ALL=C sort "$TMP/our-tree.txt" > "$TMP/our-tree-sorted.txt"
LC_ALL=C sort "$TMP/tree.txt" > "$TMP/mac-tree-sorted.txt"
if ! cmp -s "$TMP/our-tree-sorted.txt" "$TMP/mac-tree-sorted.txt"; then
    diff -u "$TMP/mac-tree-sorted.txt" "$TMP/our-tree-sorted.txt" | head -40 >&2 || true
    fail "the tree our reader sees differs from the tree macOS wrote"
fi
TREE_COUNT=$(wc -l < "$TMP/mac-tree-sorted.txt" | tr -d ' ')
echo "the tree our reader sees matches macOS: $TREE_COUNT entries"

# 5. Content: a file that needs seventeen blocks, read through its extents.
set -- $(cat "$TMP/read.txt")
READ_PATH=$1
EXPECTED_HASH=$2
GOT_HASH=$("$TOOL" read "$IMAGE" "/$READ_PATH" | shasum -a 256 | awk '{print $1}')
[ "$GOT_HASH" = "$EXPECTED_HASH" ] ||
    fail "$READ_PATH hashes to $GOT_HASH, macOS wrote $EXPECTED_HASH"
echo "$READ_PATH matches the SHA-256 macOS wrote"

# 6. The writer half. Our writer creates the same corpus on a fresh image and
#    macOS is asked to read it: names it must see, and a catalog order our B-tree
#    must have built the way Apple's did for the same names.
WIMAGE="$TMP/helikon-writer.dmg"
if ! hdiutil create -size 24m -fs HFS+J -volname HelikonWriter -layout NONE \
        "$WIMAGE" >"$TMP/wcreate.log" 2>&1; then
    cat "$TMP/wcreate.log" >&2
    fail "could not create the writer image"
fi
if ! "$HERE/hfs_image_test" "$WIMAGE" order "$TMP/names.txt" \
        > "$TMP/writer.log" 2>&1; then
    cat "$TMP/writer.log" >&2
    fail "our writer could not create the corpus"
fi
cat "$TMP/writer.log"
fsck_image "$WIMAGE"

mkdir -p "$MOUNT"
if ! hdiutil attach -nobrowse -mountpoint "$MOUNT" "$WIMAGE" \
        >"$TMP/wmount.log" 2>&1; then
    cat "$TMP/wmount.log" >&2
    fail "could not mount the writer image"
fi
MOUNTED=1
python3 - "$MOUNT" "$TMP/names.txt" <<'PY'
import os
import sys
import unicodedata

mount, names_path = sys.argv[1], sys.argv[2]
with open(names_path, encoding="utf-8") as source:
    expected = {unicodedata.normalize("NFC", line.rstrip("\n")) for line in source}
order = os.path.join(mount, "Order")
if not os.path.isdir(order):
    raise SystemExit("macOS does not see the /Order folder our writer created")
actual = {unicodedata.normalize("NFC", name) for name in os.listdir(order)}
missing = expected - actual
extra = actual - expected
if missing or extra:
    raise SystemExit(f"macOS sees different names: missing={missing!r} extra={extra!r}")
for name in os.listdir(order):
    if not os.path.isfile(os.path.join(order, name)):
        raise SystemExit(f"{name!r} is not a file on the macOS side")
print(f"macOS reads the {len(actual)} names our writer created")
PY
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$TOOL" list "$WIMAGE" /Order > "$TMP/writer-order.txt" 2>"$TMP/wlist.log" || {
    cat "$TMP/wlist.log" >&2
    fail "could not list the writer's own catalog"
}
if ! cmp -s "$TMP/catalog-order.txt" "$TMP/writer-order.txt"; then
    echo "Apple's order vs the order our writer produced:" >&2
    paste "$TMP/catalog-order.txt" "$TMP/writer-order.txt" |
        awk -F'\t' '$1 != $2' | head -20 >&2
    fail "our writer's catalog order differs from Apple's for the same names"
fi
echo "our writer's catalog order matches Apple's for $COUNT names"

echo "PASS: catalog order, canonical form, tree read, file content and a written volume macOS agrees with"
