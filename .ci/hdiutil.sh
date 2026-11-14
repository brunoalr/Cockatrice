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

# CPACK_COMMAND_HDIUTIL for CI: retries the hdiutil calls a fresh disk image fails with
# "Resource busy" (actions/runner-images#7522), and logs to $HDIUTIL_LOG what CPack hides.

self_test() {
  local dir state failed=0
  dir=$(mktemp -d)
  state=$dir/state
  mkdir "$dir/bin"
  # Plays one disk image: refusals counted in files, and the replies the real hdiutil gives.
  cat >"$dir/bin/hdiutil" <<'FAKE'
#!/bin/bash
s=$FAKE_HDIUTIL_STATE
take() {
  local n
  n=$(cat "$s/$1" 2>/dev/null || echo 0)
  (( n > 0 )) || return 1
  echo $(( n - 1 )) >"$s/$1"
}
case $1 in
  attach)
    touch "$s/attached" "$s/mounted"
    printf '/dev/disk6          \tGUID_partition_scheme          \t\n/dev/disk6s1        \tApple_HFS                      \t/Volumes/CkFlakeSelfTest\n'
    ;;
  info)
    echo $(( $(cat "$s/info_calls" 2>/dev/null || echo 0) + 1 )) >"$s/info_calls"
    [[ -f $s/info_fails ]] && exit 1
    [[ -f $s/info_killed ]] && kill -9 $$
    printf 'framework       : 0\ndriver          : 0\n'
    printf '================================================\nimage-path      : /Library/Other.dmg\n/dev/disk4\tGUID_partition_scheme\t\n/dev/disk4s1\tApple_APFS\t/Volumes/Other\n'
    if [[ -f $s/attached ]]; then
      mount=
      [[ -f $s/mounted ]] && mount=/Volumes/CkFlakeSelfTest
      printf '================================================\nimage-path      : %s\n/dev/disk6\tGUID_partition_scheme\t\n/dev/disk6s1\tApple_HFS\t%s\n' "$s/temp.dmg" "$mount"
    fi
    ;;
  detach)
    force=
    if [[ $2 == -force ]]; then
      force=1
      set -- "$1" "${@:3}"
    fi
    if [[ $2 == /dev/disk4 || $2 == /Volumes/Other ]]; then
      touch "$s/other_image_detached"
      echo '"disk4" ejected.'
    elif [[ -f $s/attached && ( $2 == /dev/disk6 || ( $2 == /Volumes/CkFlakeSelfTest && -f $s/mounted ) ) ]]; then
      if [[ -f $s/mounted ]]; then
        if [[ -z $force ]] && take unmount_refusals; then
          echo "hdiutil: couldn't unmount \"disk6\" - Resource busy" >&2
          exit 16
        fi
        rm "$s/mounted"
      fi
      if take eject_refusals; then
        echo "hdiutil: couldn't eject \"disk6\" - Resource busy" >&2
        exit 16
      fi
      rm "$s/attached"
      echo '"disk6" ejected.'
    else
      echo 'hdiutil: detach failed - No such file or directory' >&2
      exit 1
    fi
    ;;
esac
FAKE
  # Retry delays return at once; the watchdog's longer sleep still has to wait.
  cat >"$dir/bin/sleep" <<'FAKE'
#!/bin/bash
(( ${1%.*} > 30 )) && exec /bin/sleep "$1"
exit 0
FAKE
  printf '#!/bin/sh\necho "log $*"\n' >"$dir/bin/log"
  chmod +x "$dir/bin/hdiutil" "$dir/bin/sleep" "$dir/bin/log"

  wrapper() {
    PATH="$dir/bin:$PATH" FAKE_HDIUTIL_STATE="$state" TMPDIR="$state/tmp" HDIUTIL_LOG="$state/log" \
      HDIUTIL_TIMEOUT=31 bash "$0" "$@"
  }

  # check <case> <eject refusals> <unmount refusals> <state before the detach> <status> <image left attached>
  #   [<log pattern> [<most hdiutil info calls>]]
  check() {
    local name=$1 rc attached=no attach_out=
    rm -rf "$state"
    mkdir -p "$state/tmp"
    if [[ $4 != never-attached ]]; then
      attach_out=$(wrapper attach "$state/temp.dmg")
      if [[ $attach_out != *$'\t'/Volumes/CkFlakeSelfTest ]]; then
        echo "self-test FAILED: $name: attach printed '$attach_out', CPack reads the mount point from it"
        failed=1
      fi
    fi
    echo "$2" >"$state/eject_refusals"
    echo "$3" >"$state/unmount_refusals"
    case $4 in
      unmounted) rm "$state/mounted" ;;
      detached) rm "$state/mounted" "$state/attached" ;;
      detached-info-failing) rm "$state/mounted" "$state/attached" && touch "$state/info_fails" ;;
      info-timing-out) touch "$state/info_killed" ;;
    esac
    rm -f "$state/info_calls"
    wrapper detach /Volumes/CkFlakeSelfTest >/dev/null
    rc=$?
    [[ -f $state/attached ]] && attached=yes
    if [[ $rc != "$5" || $attached != "$6" || -e $state/other_image_detached ]] ||
      { [[ -n ${7:-} ]] && ! grep -q "$7" "$state/log"; } ||
      { [[ -n ${8:-} ]] && (( $(cat "$state/info_calls" 2>/dev/null || echo 0) > $8 )); }; then
      echo "self-test FAILED: $name: status $rc, image left attached: $attached (expected $5 and $6${7:+, log line: $7})"
      [[ ! -e $state/other_image_detached ]] || echo "  | another image was detached"
      [[ -z ${8:-} ]] || echo "  | hdiutil info calls: $(cat "$state/info_calls" 2>/dev/null || echo 0), at most $8 expected"
      sed 's/^/  | /' "$state/log" 2>/dev/null
      failed=1
    fi
  }

  check "idle volume" 0 0 mounted 0 no
  check "file open on the volume" 0 1 mounted 0 no
  check "eject refused after the unmount" 3 0 mounted 0 no "couldn't eject"
  check "eject refused on every attempt" 99 0 mounted 16 yes "giving up"
  check "volume unmounted before the detach" 0 0 unmounted 0 no
  check "image detached before the detach" 0 0 detached 0 no
  check "image gone but hdiutil info failing" 0 0 detached-info-failing 1 no
  check "nothing attached" 0 0 never-attached 1 no
  check "hdiutil info timing out" 99 0 info-timing-out 1 yes "giving up" 1
  rm -rf "$dir"
  if (( failed )); then
    return 1
  fi
  echo "self-test passed"
}

if [[ ${1:-} == --self-test ]]; then
  self_test
  exit
fi

attempts=10
timeout_seconds=${HDIUTIL_TIMEOUT:-300}
log=${HDIUTIL_LOG:-/dev/stderr}
# Once a detach has unmounted the volume, only the image path still leads to its device.
attached_record=${TMPDIR:-/tmp}/hdiutil-attached-image
subcommand=$1
# CPACK_DMG_VOLUME_NAME in CMakeLists.txt; a -volname argument wins when there is one.
volume_name=Cockatrice
arg_count=$#
for (( i = 1; i < arg_count; i++ )); do
  if [[ ${!i} == -volname ]] && (( i + 1 <= arg_count )); then
    (( i++ ))
    volume_name=${!i}
  fi
done

note() {
  echo "$(date '+%H:%M:%S') hdiutil.sh: $*" >>"$log"
}

# macOS ships no timeout(1); the watchdog writes to /dev/null so it cannot hold CPack's pipe open.
run_supervised() {
  local pid watchdog status
  "$@" &
  pid=$!
  ( sleep "$timeout_seconds" && kill -9 "$pid" ) >/dev/null 2>&1 &
  watchdog=$!
  wait "$pid"
  status=$?
  kill "$watchdog" 2>/dev/null
  wait "$watchdog" 2>/dev/null
  return "$status"
}

force_detach_stale_mount() {
  [[ -d /Volumes/$volume_name ]] || return 0
  note "force-detaching stale /Volumes/$volume_name"
  run_supervised hdiutil detach -force "/Volumes/$volume_name" >>"$log" 2>&1
  return 0
}

# Prints "<whole disk><tab><image path>" of the image whose $1 (image, device or mount) is $2.
# Returns 2 if hdiutil info fails, 3 if it timed out.
find_image() {
  local info status
  [[ -n $2 ]] || return 0
  # A DiskImages helper wedged enough to time a call out can hang hdiutil info as well.
  (( timeouts == 0 )) || return 2
  info=$(run_supervised hdiutil info 2>>"$log")
  status=$?
  (( status != 137 )) || return 3
  (( status == 0 )) && [[ -n $info ]] || return 2
  awk -F '\t' -v key="$1" -v want="$2" '
    /^image-path/ { sub(/^image-path *: /, ""); image = $0; device = ""; next }
    /^\/dev\// {
      if (device == "") device = $1
      if ((key == "image" && image == want) || (key == "device" && $1 == want) || (key == "mount" && $3 == want)) {
        print device "\t" image
        exit
      }
    }' <<<"$info"
}

# Sets found in this shell, where a timed-out hdiutil info can count as a timed-out call.
lookup() {
  local status
  found=$(find_image "$@")
  status=$?
  (( status != 3 )) || (( timeouts++ ))
  return "$status"
}

diagnose() {
  local raw=${device/disk/rdisk} timeout_seconds=60
  {
    (( attempt > 1 || timeouts > 0 )) || run_supervised hdiutil info
    [[ -n $device ]] && run_supervised lsof "$device" "$device"s* "$raw" "$raw"s*
    [[ -d $target ]] && run_supervised lsof "$target"
    ps -o pid,user,etime,comm -p "$(pgrep -d, -i 'diskimages|xprotect|mds|iconservices|finder|fseventsd|diskarbitration')"
    run_supervised log show --start "$since" --style compact --predicate 'process == "diskarbitrationd"'
  } >>"$log" 2>&1
}

timeouts=0
image=
device=
if [[ $subcommand == detach ]]; then
  target=${!#}
  lookup mount "$target"
  image=${found#*$'\t'}
  device=${found%%$'\t'*}
  [[ -n $image ]] || image=$(cat "$attached_record" 2>/dev/null)
  note "detaching $target: image ${image:-unknown}, device ${device:-unknown}"
fi

attempt=0
delay=1
while (( attempt < attempts )); do
  (( attempt++ ))
  cmd=(hdiutil "$@")
  # -force clears a busy volume; the device outlives a detach that unmounted but did not eject.
  if (( attempt > 1 )) && [[ $subcommand == detach ]]; then
    if [[ -n $device ]]; then
      cmd=(hdiutil detach -force "$device")
    else
      cmd=(hdiutil detach -force "${@:2}")
    fi
  fi
  note "attempt $attempt/$attempts: ${cmd[*]}"
  since=$(date '+%Y-%m-%d %H:%M:%S')
  output=$(run_supervised "${cmd[@]}" 2>>"$log")
  rc=$?
  [[ -z $output ]] || printf '%s\n' "$output" | tee -a "$log"
  note "status $rc"
  if (( rc == 0 )); then
    if [[ $subcommand == attach ]] &&
      lookup device "$(awk '/^\/dev\/disk/ { print $1; exit }' <<<"$output")" && [[ -n $found ]]; then
      printf '%s\n' "${found#*$'\t'}" >"$attached_record"
    elif [[ $subcommand == attach || $subcommand == detach ]]; then
      rm -f "$attached_record"
    fi
    exit 0
  fi
  if (( rc == 137 )); then
    (( timeouts++ ))
    note "attempt $attempt/$attempts timed out after ${timeout_seconds}s"
    # A wedged DiskImages helper is not the busy-file flake: fail after two kills.
    if (( timeouts >= 2 )); then
      echo "::error::hdiutil $subcommand timed out $timeouts times, giving up" >>"$log"
      exit "$rc"
    fi
  fi
  if [[ $subcommand == detach ]]; then
    if [[ -n $image ]] && lookup image "$image"; then
      if [[ -z $found ]]; then
        note "$image is no longer attached"
        rm -f "$attached_record"
        exit 0
      fi
      device=${found%%$'\t'*}
    fi
    diagnose
  fi
  (( attempt < attempts )) || break
  if [[ $subcommand == create || $subcommand == attach ]]; then
    force_detach_stale_mount
  fi
  sleep "$delay"
  (( delay = delay * 2 > 30 ? 30 : delay * 2 ))
done
# CPack reads this status: zero would surface later as a misleading "Problem copying the package".
echo "::error::hdiutil $subcommand failed $attempt times (last status $rc), giving up" >>"$log"
exit "$rc"
