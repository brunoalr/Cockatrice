#!/bin/bash
set -uo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <directory> [page]" >&2
  exit 3
fi
dir=$1
page=${2:-index.html}
chrome=${CHROME:-google-chrome}
port=${WASM_SMOKE_PORT:-8137}
devtools_port=${WASM_SMOKE_DEVTOOLS_PORT:-9237}
start_seconds=${START_SECONDS:-120}
settle_seconds=${SETTLE_SECONDS:-15}
started='ui\.show\(\) finished'
fatal='Aborted\(|Uncaught|is not supported on Qt for WebAssembly'
not_isolated='is not supported for now'
serve_flag=
if [[ ${WASM_SMOKE_ISOLATION_HEADERS:-1} == 0 ]]; then
  serve_flag=--no-isolation-headers
fi

work=$(mktemp -d)
log="$work/chrome.log"
: >"$log"
python3 "$(dirname "$0")/wasm_serve.py" "$port" "$dir" ${serve_flag:+"$serve_flag"} >"$work/server.log" &
server=$!
for ((attempt = 0; attempt < 20; attempt++)); do
  if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
    break
  fi
  sleep 0.5
done
# shellcheck disable=SC2086
"$chrome" --headless=new --no-first-run --disable-extensions --enable-logging=stderr --v=0 ${CHROME_FLAGS:-} \
  --remote-debugging-port="$devtools_port" --user-data-dir="$work/profile" "http://127.0.0.1:$port/$page" >"$log" 2>&1 &
browser=$!

result="no start-up within $start_seconds s"
started_at=
for ((second = 0; second <= start_seconds + settle_seconds; second++)); do
  if grep -aqE "$fatal" "$log"; then
    result="the page aborted"
    break
  fi
  if grep -aqF "$not_isolated" "$log"; then
    result="the page is not cross-origin isolated"
    # a reload loop would show up as more page loads
    sleep "$settle_seconds"
    break
  fi
  if ! kill -0 "$browser" 2>/dev/null; then
    result="the browser exited"
    break
  fi
  if [[ -z $started_at ]] && grep -aqE "$started" "$log"; then
    started_at=$second
  fi
  if [[ -n $started_at ]] && ((second - started_at >= settle_seconds)); then
    result=started
    break
  fi
  if [[ -z $started_at ]] && ((second >= start_seconds)); then
    break
  fi
  sleep 1
done

if [[ $result == started ]]; then
  if ! node --experimental-websocket "$(dirname "$0")/wasm_open_connect.mjs" "$devtools_port" "$settle_seconds"; then
    result="the connect dialog did not open"
  else
    sleep "$settle_seconds"
    if grep -aqE "$fatal" "$log"; then
      result="the page aborted after the connect dialog opened"
    elif ! kill -0 "$browser" 2>/dev/null; then
      result="the browser exited after the connect dialog opened"
    fi
  fi
fi

kill "$browser" "$server" 2>/dev/null
wait "$browser" "$server" 2>/dev/null
grep -a 'CONSOLE' "$log" | sed -E 's/^\[[^]]*\] //; s/", source: .*//' | cut -c1-400
if [[ -n $serve_flag ]]; then
  loads=$(grep -cF "GET /$page HTTP" "$work/server.log")
  echo "page loads: $loads"
  # the isolation worker reloads the page once
  if ((loads > 2)); then
    result="the page loaded $loads times"
  fi
fi
if [[ $result != started ]]; then
  echo "--- last lines of the browser log"
  tail -n 40 "$log" | cut -c1-300
fi
rm -rf "$work"
echo "smoke test: $result"
[[ $result == started ]]
