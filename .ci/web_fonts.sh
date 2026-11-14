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

raw=https://raw.githubusercontent.com
noto=$raw/notofonts/notofonts.github.io/92d2ea744479d3379927f1fb5f8b0945dd0349b7/fonts
names=() urls=() sums=() licence_urls=() licence_sums=() notices=()

# <name on the site> <font> <its SHA-256> <licence text> <its SHA-256> [<copyright notice the licence text lacks>]
font() {
  names+=("$1") urls+=("$2") sums+=("$3") licence_urls+=("$4") licence_sums+=("$5") notices+=("${6:-}")
}

adobe_notice="Copyright © 2014-2021 Adobe (http://www.adobe.com/)." # NOSONAR: the font's own copyright line
font NotoSansThai-Regular.ttf "$noto/NotoSansThai/unhinted/ttf/NotoSansThai-Regular.ttf" \
  d4303fe9c63ebb72759ca8b6d2040c8ae81689f7d08d7b91c656154382b49313 \
  "$raw/notofonts/thai/f8b482c158650260bba5d5edba9da3e8bb7185b4/OFL.txt" \
  2e98fd23a52d253db8612cd5942c8f2ff4111b21d2367050fdca91d8ccc374a0
font NotoSansDevanagari-Regular.ttf "$noto/NotoSansDevanagari/unhinted/ttf/NotoSansDevanagari-Regular.ttf" \
  9c7d935139ea6a1e6ad9dbac4f6d27ece1e04bca8123c8888d00a0f9df4724cd \
  "$raw/notofonts/devanagari/e123d230c160ebe949d731cc19017cdb354180d1/OFL.txt" \
  a216f6f8d85c7228093e0ee5e258d9d377e6671f68acb4db1930b29583d0f331
font NotoEmoji.ttf "$raw/google/fonts/b979dba422e445492b0eb9951ac52ee0b4d648c3/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf" \
  de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551 \
  "$raw/google/fonts/b979dba422e445492b0eb9951ac52ee0b4d648c3/ofl/notoemoji/OFL.txt" \
  500bb1ccf43df7bbb522112f9133a52b16e1c35e809632f5d8609b179152de5b
font NotoSansCJK-Regular.ttc "$raw/notofonts/noto-cjk/a99a4354c68964f6bfac488d01010b1fb6d9178a/Sans/OTC/NotoSansCJK-Regular.ttc" \
  b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a \
  "$raw/notofonts/noto-cjk/a99a4354c68964f6bfac488d01010b1fb6d9178a/Sans/LICENSE" \
  6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2 \
  "$adobe_notice"

sha256() {
  local file=$1
  if command -v sha256sum >/dev/null; then
    sha256sum "$file" | cut -c1-64
  else
    shasum -a 256 "$file" | cut -c1-64
  fi
}

get() {
  local url=$1 sum=$2 file=$3 got
  if ! curl -fsSL --retry 3 --connect-timeout 20 --max-time 300 -o "$file.part" "$url"; then
    rm -f "$file.part"
    echo "::error::cannot download $url"
    exit 1
  fi
  got=$(sha256 "$file.part")
  if [[ $got != "$sum" ]]; then
    rm -f "$file.part"
    echo "::error::$url has the SHA-256 $got, not the $sum this script expects"
    exit 1
  fi
  mv "$file.part" "$file"
}

download() {
  local dir=$1 i wanted listed=
  shift
  for wanted; do
    if [[ " ${names[*]} " != *" $wanted "* ]]; then
      echo "::error::$wanted is not one of the fonts: ${names[*]}"
      exit 1
    fi
  done
  mkdir -p "$dir"
  for i in "${!names[@]}"; do
    if [[ $# -gt 0 && " $* " != *" ${names[i]} "* ]]; then
      continue
    fi
    # A file kept from an earlier run is used again only as long as it is still the expected one
    if [[ ! -f $dir/${names[i]} || $(sha256 "$dir/${names[i]}") != "${sums[i]}" ]]; then
      get "${urls[i]}" "${sums[i]}" "$dir/${names[i]}"
    fi
    get "${licence_urls[i]}" "${licence_sums[i]}" "$dir/licence"
    {
      if [[ -n ${notices[i]} ]]; then
        printf '%s\n\n' "${notices[i]}"
      fi
      cat "$dir/licence"
    } >"$dir/${names[i]%.*}.LICENSE.txt"
    rm "$dir/licence"
    listed+="${names[i]}"$'\n'
  done
  printf '%s' "$listed" >"$dir/index.txt"
  echo "$dir holds $(tr '\n' ' ' <"$dir/index.txt")"
}

self_test() {
  local work port
  work=$(mktemp -d)
  port=${WEB_FONTS_TEST_PORT:-8139}
  # shellcheck disable=SC2064
  trap "rm -rf '$work'; { kill \${server:-} && wait \${server:-}; } 2>/dev/null || true" EXIT
  local served="$work/served" out="$work/out" requests="$work/requests"
  mkdir -p "$served"
  python3 "$(dirname "$0")/wasm_serve.py" "$port" "$served" >"$requests" &
  server=$!
  local attempt
  for ((attempt = 0; attempt < 20; attempt++)); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
      break
    fi
    sleep 0.5
  done
  if ! kill -0 "$server" 2>/dev/null; then
    echo "self-test FAILED: no fixture server on port $port"
    return 1
  fi
  local url="http://127.0.0.1:$port"

  local name file
  for name in NotoSansThai-Regular.ttf NotoSansCJK-Regular.ttc; do
    if [[ ! " ${names[*]} " == *" $name "* ]]; then
      echo "self-test FAILED: $name is not among the fonts"
      return 1
    fi
  done
  for i in "${!names[@]}"; do
    if [[ ! ${names[i]} =~ ^[A-Za-z0-9][A-Za-z0-9._-]*\.(ttf|otf|ttc)$ ]]; then
      echo "self-test FAILED: ${names[i]} is not a plain font file name the client accepts"
      return 1
    fi
    for file in "${urls[i]}" "${licence_urls[i]}"; do
      if [[ ! $file =~ ^https://raw\.githubusercontent\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/[0-9a-f]{40}/ ]]; then
        echo "self-test FAILED: $file is not pinned to a commit"
        return 1
      fi
    done
    for file in "${sums[i]}" "${licence_sums[i]}"; do
      if [[ ! $file =~ ^[0-9a-f]{64}$ ]]; then
        echo "self-test FAILED: $file is not a SHA-256"
        return 1
      fi
    done
  done

  printf 'font a' >"$served/a.ttf"
  printf 'font b' >"$served/b.ttc"
  printf 'licence a\n' >"$served/a-licence.txt"
  printf 'licence b\n' >"$served/b-licence.txt"
  printf 'font a' >"$work/a.ttf"
  fixture() {
    names=() urls=() sums=() licence_urls=() licence_sums=() notices=()
    font a.ttf "$url/a.ttf" "$(sha256 "$work/a.ttf")" "$url/a-licence.txt" "$(sha256 "$served/a-licence.txt")"
    font b.ttc "$url/b.ttc" "${1:-$(sha256 "$served/b.ttc")}" "$url/b-licence.txt" "$(sha256 "$served/b-licence.txt")" \
      'Copyright b'
  }
  downloads() {
    local dir=$1
    shift
    if ! (fixture && download "$dir" "$@") >"$out" 2>&1; then
      echo "self-test FAILED: downloading into $dir failed:"
      cat "$out"
      return 1
    fi
  }
  requests_for() {
    local name=$1
    grep -c "GET /$name " "$requests" || true
  }

  downloads "$work/all"
  if [[ $(<"$work/all/index.txt") != $'a.ttf\nb.ttc' ]]; then
    echo "self-test FAILED: index.txt holds '$(<"$work/all/index.txt")'"
    return 1
  fi
  if [[ $(<"$work/all/a.ttf") != "font a" || $(<"$work/all/b.ttc") != "font b" ]] ||
    [[ $(<"$work/all/a.LICENSE.txt") != "licence a" || $(<"$work/all/b.LICENSE.txt") != $'Copyright b\n\nlicence b' ]]; then
    echo "self-test FAILED: the directory does not hold the fonts and their licences: $(cd "$work/all" && echo ./*)"
    return 1
  fi
  if [[ $(cd "$work/all" && echo ./*) != "./a.LICENSE.txt ./a.ttf ./b.LICENSE.txt ./b.ttc ./index.txt" ]]; then
    echo "self-test FAILED: the directory holds $(cd "$work/all" && echo ./*)"
    return 1
  fi

  downloads "$work/all"
  if [[ $(requests_for a.ttf) != 1 || $(requests_for a-licence.txt) != 2 ]]; then
    echo "self-test FAILED: a font already there was downloaded again, or its licence was not"
    return 1
  fi
  printf 'altered' >"$work/all/a.ttf"
  downloads "$work/all"
  if [[ $(requests_for a.ttf) != 2 || $(<"$work/all/a.ttf") != "font a" ]]; then
    echo "self-test FAILED: an altered font was kept"
    return 1
  fi

  downloads "$work/one" b.ttc
  if [[ $(<"$work/one/index.txt") != "b.ttc" || -e $work/one/a.ttf ]]; then
    echo "self-test FAILED: a download of b.ttc alone holds $(cd "$work/one" && echo ./*)"
    return 1
  fi
  if (fixture && download "$work/x1" c.ttf) >"$out" 2>&1 || ! grep -q 'c.ttf is not one of the fonts' "$out"; then
    echo "self-test FAILED: an unknown font was not refused: $(<"$out")"
    return 1
  fi

  if (fixture 0000000000000000000000000000000000000000000000000000000000000000 && download "$work/x2") >"$out" 2>&1; then
    echo "self-test FAILED: a font with another checksum was accepted"
    return 1
  fi
  if ! grep -q "::error::$url/b.ttc has the SHA-256" "$out" || [[ -e $work/x2/b.ttc || -e $work/x2/b.ttc.part ]]; then
    echo "self-test FAILED: a font with another checksum was not named, or was left behind: $(<"$out")"
    return 1
  fi
  rm "$served/b-licence.txt"
  if (fixture && download "$work/x3") >"$out" 2>&1 || ! grep -q "::error::cannot download $url/b-licence.txt" "$out"; then
    echo "self-test FAILED: a missing licence was not refused: $(<"$out")"
    return 1
  fi
  echo "self-test passed"
}

case "${1:-}" in
  --self-test)
    self_test
    ;;
  '' | -*)
    echo "usage: $0 <directory> [<font>...] | --self-test" >&2
    exit 3
    ;;
  *)
    download "$@"
    ;;
esac
