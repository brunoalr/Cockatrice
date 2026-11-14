#!/bin/bash
# Copyright (C) 2026 by Bruno Alexandre Rosa
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

set -euo pipefail

# Under half of what Oracle wrote in November 2026: cards.xml 65.1 MB, tokens.xml 1.33 MB
cards_floor=$((30 * 1000 * 1000))
tokens_floor=$((600 * 1000))

# Prints the root element's name and the number of tags elements; fails on a document that is not well-formed
inspect_xml() {
  local path=$1
  python3 - "$path" <<'EOF'
import sys
import xml.etree.ElementTree as ElementTree

root = None
tags = 0
for event, element in ElementTree.iterparse(sys.argv[1], events=("start", "end")):
    if event == "start":
        if root is None:
            root = element.tag
    else:
        if element.tag == "tags":
            tags += 1
        element.clear()
print(root, tags)
EOF
}

validate_card_database() {
  local dir=$1 failed=0 name floor path size root_name tags
  for name in cards.xml tokens.xml; do
    floor=$cards_floor
    [[ $name == tokens.xml ]] && floor=$tokens_floor
    path=$dir/$name
    if [[ ! -f $path ]]; then
      echo "::error::$path is missing"
      failed=1
      continue
    fi
    size=$(wc -c <"$path")
    if ((size < floor)); then
      echo "::error::$path holds $((size)) bytes, below the floor of $floor"
      failed=1
      continue
    fi
    if ! read -r root_name tags < <(inspect_xml "$path" 2>/dev/null); then
      echo "::error::$path is not well-formed XML"
      failed=1
      continue
    fi
    if [[ $root_name != cockatrice_carddatabase ]]; then
      echo "::error::$path does not have cockatrice_carddatabase as its root element"
      failed=1
      continue
    fi
    # Oracle finishes without card tags when their download fails
    if [[ $name == cards.xml ]] && ((tags == 0)); then
      echo "::error::$path holds no card tags"
      failed=1
    fi
  done
  return "$failed"
}

build_card_database() {
  local out=$1 build=${BUILD_DIR:-build-oracle} status=0
  cmake -S "$(dirname "$0")/.." -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DWITH_CLIENT=0 -DWITH_ORACLE=1 -DWITH_SERVER=0 -DTEST=0 -DUSE_CCACHE=1 -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
  # moc, uic and rcc write their output after ccache starts; without these every file including one misses
  CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \
    cmake --build "$build" --target oracle

  # portable.dat sends Oracle's settings and output to <binary dir>/data; a previous run's files must not pass the check
  local data=$build/oracle/data
  rm -rf "$data"
  touch "$build/oracle/portable.dat"
  QT_QPA_PLATFORM=offscreen timeout 900 "$build/oracle/oracle" -b || status=$?
  if ((status == 124)); then
    echo "::error::Oracle did not finish within 900 s"
    return 1
  fi
  if ((status != 0)); then
    echo "::error::Oracle exited with $status"
    return 1
  fi
  validate_card_database "$data"
  mkdir -p "$out"
  cp "$data/cards.xml" "$data/tokens.xml" "$out/"
}

make_database() {
  local file=$1 root=$2 padding=$3 body=${4:-} prolog=${5:-}
  {
    printf '<?xml version="1.0" encoding="UTF-8"?>\n%s\n<%s version="4">\n%s\n' "$prolog" "$root" "$body"
    head -c "$padding" /dev/zero | tr '\0' ' '
    printf '\n</%s>\n' "$root"
  } >"$file"
}

expect_failure() {
  local dir=$1 file=$2 fixture=$3 output
  if output=$(validate_card_database "$dir" 2>&1); then
    echo "self-test FAILED: $fixture passed the check"
    return 1
  fi
  if [[ $output != *"$file"* ]]; then
    echo "self-test FAILED: $fixture failed without naming $file: $output"
    return 1
  fi
}

self_test() {
  # not local: the trap runs after the function's variables are gone
  root=$(mktemp -d)
  trap 'rm -rf "$root"' EXIT
  mkdir "$root/good" "$root/no-cards" "$root/no-tokens" "$root/small-cards" "$root/small-tokens" "$root/other-root" \
    "$root/no-tags" "$root/cut-short" "$root/root-in-comment"
  make_database "$root/good/cards.xml" cockatrice_carddatabase "$cards_floor" '<tags/>'
  make_database "$root/good/tokens.xml" cockatrice_carddatabase "$tokens_floor"
  ln "$root/good/tokens.xml" "$root/no-cards/tokens.xml"
  ln "$root/good/cards.xml" "$root/no-tokens/cards.xml"
  make_database "$root/small-cards/cards.xml" cockatrice_carddatabase "$((cards_floor / 2))"
  ln "$root/good/tokens.xml" "$root/small-cards/tokens.xml"
  ln "$root/good/cards.xml" "$root/small-tokens/cards.xml"
  make_database "$root/small-tokens/tokens.xml" cockatrice_carddatabase "$((tokens_floor / 2))"
  ln "$root/good/cards.xml" "$root/other-root/cards.xml"
  make_database "$root/other-root/tokens.xml" cockatrice_deck "$tokens_floor"
  make_database "$root/no-tags/cards.xml" cockatrice_carddatabase "$cards_floor"
  ln "$root/good/tokens.xml" "$root/no-tags/tokens.xml"
  head -c "$cards_floor" "$root/good/cards.xml" >"$root/cut-short/cards.xml"
  ln "$root/good/tokens.xml" "$root/cut-short/tokens.xml"
  make_database "$root/root-in-comment/cards.xml" other "$cards_floor" '<!-- <cockatrice_carddatabase> <tags> -->' \
    '<!-- <cockatrice_carddatabase> -->'
  ln "$root/good/tokens.xml" "$root/root-in-comment/tokens.xml"

  local output
  if ! output=$(validate_card_database "$root/good" 2>&1); then
    echo "self-test FAILED: a good pair failed the check: $output"
    return 1
  fi
  expect_failure "$root/no-cards" cards.xml "a missing cards.xml" || return 1
  expect_failure "$root/no-tokens" tokens.xml "a missing tokens.xml" || return 1
  expect_failure "$root/small-cards" cards.xml "a cards.xml below the floor" || return 1
  expect_failure "$root/small-tokens" tokens.xml "a tokens.xml below the floor" || return 1
  expect_failure "$root/other-root" tokens.xml "a tokens.xml with another root element" || return 1
  expect_failure "$root/no-tags" cards.xml "a cards.xml without card tags" || return 1
  expect_failure "$root/cut-short" cards.xml "a cards.xml cut short" || return 1
  expect_failure "$root/root-in-comment" cards.xml "a cards.xml that names the root and tags only in comments" || return 1
  echo "self-test passed"
}

if ! command -v python3 >/dev/null; then
  echo "::error::python3 is needed to check the card database" >&2
  exit 3
fi

case "${1:-}" in
  --self-test)
    self_test
    ;;
  '' | -*)
    echo "usage: $0 <output-dir> | --self-test" >&2
    exit 3
    ;;
  *)
    # A macOS build puts Oracle in an app bundle, where portable.dat beside the build would not apply
    if [[ $(uname) != Linux ]]; then
      echo "building the card database runs on Linux only" >&2
      exit 3
    fi
    build_card_database "$1"
    ;;
esac
