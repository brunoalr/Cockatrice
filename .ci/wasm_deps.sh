#!/bin/bash
set -euo pipefail

if [[ -z ${EMSDK:-} ]]; then
  echo "::error file=$0::EMSDK is not set, source emsdk_env.sh first" >&2
  exit 3
fi

root=$(cd "$(dirname "$0")/.." && pwd)
vcpkg="$root/vcpkg"
triplet=wasm32-emscripten-mt
triplets=$(mktemp -d)
trap 'rm -rf "$triplets"' EXIT

{
  cat "$vcpkg/triplets/community/wasm32-emscripten.cmake"
  echo
  echo 'set(VCPKG_C_FLAGS "-pthread")'
  echo 'set(VCPKG_CXX_FLAGS "-pthread")'
  echo 'set(VCPKG_LINKER_FLAGS "-pthread")'
} >"$triplets/$triplet.cmake"

{
  if [[ ! -x $vcpkg/vcpkg ]]; then
    "$vcpkg/bootstrap-vcpkg.sh" -disableMetrics
  fi
  # vcpkg hashes the tools it builds with into every package: its own pinned ones keep that hash stable
  # across runner images, where the runner's cmake would not
  "$vcpkg/vcpkg" install --classic protobuf openssl --triplet "$triplet" --overlay-triplets="$triplets" \
    --x-abi-tools-use-exact-versions
} >&2

deps="$vcpkg/installed/$triplet"
protoc=$(find "$vcpkg/installed" -path '*/tools/protobuf/protoc' -not -path "*/$triplet/*" | head -n 1)
if [[ ! -f $deps/lib/libprotobuf.a || ! -f $deps/lib/libcrypto.a || ! -x $protoc ]]; then
  echo "::error file=$0::vcpkg did not produce libprotobuf.a, libcrypto.a and a host protoc" >&2
  exit 1
fi

echo "WASM_DEPS_DIR=$deps"
echo "WASM_PROTOC=$protoc"
