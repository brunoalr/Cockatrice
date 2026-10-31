#!/bin/bash

# The macos binaries from aqt are fat (universal), so we thin them to the target architecture to reduce the size of
# the packages and caches using lipo.

# This script is meant to be used by the ci enironment on macos runners only.
# It uses the runner's GITHUB_WORKSPACE env variable.
# 'soc' argument specifies the target system on chip (SoC) architecture, e.g., 'Intel' for x86_64
# Example usage:
#   ./thin_macos_qtlib.sh Intel    # Thins binaries to x86_64
#   ./thin_macos_qtlib.sh Apple    # Thins binaries to arm64


soc=$1
nproc=$(sysctl -n hw.ncpu)

if [[ $soc == 'Intel' ]]; then
  arch='x86_64'
elif [[ $soc == 'Apple' ]]; then
  arch='arm64'
else
  echo "Invalid 'soc' argument: $soc"
  echo "Usage: ./thin_macos_qtlib.sh [Intel|Apple]"
  exit 1
fi

function thin() {
  local libfile=$1
  # moc links no Qt framework, so the fat one still loads after thinning the rest. Its arm64 slice runs
  # natively on the runner, its x86_64 slice only under Rosetta, which is ~3x slower on the build's critical path.
  if [[ $soc == 'Intel' && $libfile == */libexec/moc ]]; then
    return 0
  fi
  if [[ $(file -b --mime-type "$libfile") == application/x-mach-binary* ]]; then
    echo "Processing $libfile"
    lipo "$libfile" -thin "$arch" -output "$libfile"
  fi
  return 0
}
export -f thin  # export to allow use in xargs
export arch soc
set -eo pipefail

echo "::group::Thinning Qt libraries to $arch using $nproc cores"
find "$GITHUB_WORKSPACE/Qt" -type f -print0 | xargs -0 -n1 -P"$nproc" -I{} bash -c "thin '{}'"
echo "::endgroup::"
