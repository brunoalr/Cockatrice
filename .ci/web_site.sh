#!/bin/bash
set -euo pipefail
# cd prints the directory it found through CDPATH, and that text would enter the hash
unset CDPATH

app_files=(cockatrice.js cockatrice.wasm qtloader.js qtlogo.svg)

sha256() {
  if command -v sha256sum >/dev/null; then
    sha256sum
  else
    shasum -a 256
  fi
}

require_files() {
  local dir=$1 file
  shift
  for file; do
    if [[ ! -s $dir/$file ]]; then
      echo "::error::$dir/$file is missing or empty"
      exit 1
    fi
  done
}

app_hash() {
  (cd "$1" && cat "${app_files[@]}") | sha256 | cut -c1-16
}

keep_previous() {
  local url=${1%/} site=$2 response status live file
  local version='^[0-9a-f]{16}$' version_or_null='^("[0-9a-f]{16}"|null)$'
  if ! response=$(curl -sSL --retry 3 -w '%{http_code}' "$url/versions.json"); then
    echo "::error::cannot read $url/versions.json"
    exit 1
  fi
  status=${response: -3}
  case $status in
    404)
      echo "$url/versions.json is not there: no site yet, no previous version to keep"
      return
      ;;
    200) ;;
    *)
      echo "::error::$url/versions.json answered HTTP $status"
      exit 1
      ;;
  esac
  live=${response%???}
  previous=$(sed -nE 's/.*"current": *"([0-9a-f]{16})".*/\1/p' <<<"$live")
  if [[ ! $previous =~ $version ]]; then
    echo "::error::$url/versions.json names no current version"
    exit 1
  fi
  if [[ $previous == "$hash" ]]; then
    previous=$(sed -nE 's/.*"previous": *("[0-9a-f]{16}"|null).*/\1/p' <<<"$live")
    if [[ ! $previous =~ $version_or_null ]]; then
      echo "::error::$url/versions.json names no previous version"
      exit 1
    fi
    previous=${previous//\"/}
    if [[ $previous == null ]]; then
      previous=
      return
    fi
  fi
  mkdir "$site/app/$previous"
  for file in "${app_files[@]}"; do
    if ! curl -fsSL --retry 3 -o "$site/app/$previous/$file" "$url/app/$previous/$file"; then
      echo "::error::cannot download $url/app/$previous/$file"
      exit 1
    fi
  done
  if [[ $(app_hash "$site/app/$previous") != "$previous" ]]; then
    echo "::error::the files in $url/app/$previous/ do not match their version"
    exit 1
  fi
  echo "kept the previous version $previous from $url"
}

assemble() {
  local app=$1 database=$2 site=${3%/} previous_url=${4:-}
  local web worker hash previous=
  web="$(dirname "$0")/../cockatrice/web"
  worker="$web/coi-serviceworker.js"
  require_files "$app" "${app_files[@]}"
  require_files "$database" cards.xml tokens.xml
  require_files "$web" index.html coi-serviceworker.js
  if [[ $(head -n 1 "$worker" | grep -oE '[0-9a-f]{64}') != "$(tail -n +2 "$worker" | sha256 | cut -c1-64)" ]]; then
    echo "::error::$worker differs from the upstream file its first line names"
    exit 1
  fi
  if [[ -n $(ls -A "$site" 2>/dev/null) ]]; then
    echo "::error::$site is not empty"
    exit 1
  fi

  mkdir -p "$(dirname "$site")"
  staging=$(mktemp -d "$site.XXXXXX")
  trap 'rm -rf "$staging"' EXIT
  # mktemp -d makes the directory private, and the site is published as it is
  chmod 755 "$staging"
  hash=$(app_hash "$app")
  mkdir -p "$staging/app/$hash"
  cp "${app_files[@]/#/$app/}" "$staging/app/$hash/"
  cp "$database/cards.xml" "$database/tokens.xml" "$worker" "$staging/"
  sed "s|app/@HASH@/|app/$hash/|g" "$web/index.html" >"$staging/index.html"
  if ! grep -q "app/$hash/" "$staging/index.html" || grep -q '@HASH@' "$staging/index.html"; then
    echo "::error::$web/index.html does not name app/@HASH@/"
    exit 1
  fi
  if [[ -n $previous_url ]]; then
    keep_previous "$previous_url" "$staging"
  fi
  if [[ -n $previous ]]; then
    previous="\"$previous\""
  else
    previous=null
  fi
  printf '{"current": "%s", "previous": %s}\n' "$hash" "$previous" >"$staging/versions.json"
  if [[ -d $site ]]; then
    rmdir "$site"
  fi
  mv "$staging" "$site"
  echo "assembled version $hash in $site"
}

self_test() {
  local work port
  work=$(mktemp -d)
  port=${WEB_SITE_TEST_PORT:-8138}
  # shellcheck disable=SC2064
  trap "rm -rf '$work'; { kill \${server:-} && wait \${server:-}; } 2>/dev/null || true" EXIT
  local script="$work/repo/.ci/web_site.sh" web="$work/repo/cockatrice/web" served="$work/served" out="$work/out"
  mkdir -p "$work/repo/.ci" "$web" "$work/app" "$work/database" "$served"
  cp "$0" "$script"
  cp "$(dirname "$0")"/../cockatrice/web/{index.html,coi-serviceworker.js} "$web/"
  local file
  for file in "${app_files[@]}"; do
    echo "fixture $file" >"$work/app/$file"
  done
  for file in cards.xml tokens.xml; do
    echo '<cockatrice_carddatabase version="4"></cockatrice_carddatabase>' >"$work/database/$file"
  done

  python3 "$(dirname "$0")/wasm_serve.py" "$port" "$served" >/dev/null &
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

  assembles() {
    if ! bash "$script" "$@" >"$out" 2>&1; then
      echo "self-test FAILED: assembling $3 failed:"
      cat "$out"
      return 1
    fi
  }
  refuses() {
    local what=$1
    shift
    if bash "$script" "$@" >"$out" 2>&1; then
      echo "self-test FAILED: $what was assembled"
      return 1
    fi
  }
  versions_of() {
    ls "$1/app"
  }

  local a
  a=$(cd "$work/app" && cat "${app_files[@]}" | sha256 | cut -c1-16)
  assembles "$work/app" "$work/database" "$work/a"
  for file in index.html coi-serviceworker.js versions.json cards.xml tokens.xml "${app_files[@]/#/app/$a/}"; do
    if [[ ! -s $work/a/$file ]]; then
      echo "self-test FAILED: the site has no $file"
      return 1
    fi
  done
  if [[ $(versions_of "$work/a") != "$a" ]]; then
    echo "self-test FAILED: the site holds versions '$(versions_of "$work/a")', expected $a"
    return 1
  fi
  if ! grep -q "src=\"app/$a/cockatrice.js\"" "$work/a/index.html" || grep -q '@HASH@' "$work/a/index.html"; then
    echo "self-test FAILED: index.html does not load app/$a/"
    return 1
  fi
  if [[ $(<"$work/a/versions.json") != "{\"current\": \"$a\", \"previous\": null}" ]]; then
    echo "self-test FAILED: versions.json holds $(<"$work/a/versions.json")"
    return 1
  fi
  if ! (cd "$work" && CDPATH=$work bash "$script" app database a-cdpath >"$out" 2>&1); then
    echo "self-test FAILED: assembling with CDPATH exported failed: $(<"$out")"
    return 1
  fi
  if [[ $(versions_of "$work/a-cdpath") != "$a" ]]; then
    echo "self-test FAILED: with CDPATH exported the site holds versions '$(versions_of "$work/a-cdpath")', expected $a"
    return 1
  fi

  echo '<cockatrice_carddatabase version="4"><cards/></cockatrice_carddatabase>' >"$work/database/cards.xml"
  assembles "$work/app" "$work/database" "$work/a-database"
  if [[ $(versions_of "$work/a-database") != "$a" ]]; then
    echo "self-test FAILED: a database change changed the version"
    return 1
  fi

  cp -R "$work/app" "$work/app-b"
  printf '\0' >>"$work/app-b/cockatrice.wasm"
  cp -R "$work/a" "$served/a"
  assembles "$work/app-b" "$work/database" "$work/b" "$url/a"
  local b
  b=$(<"$work/b/versions.json")
  b=${b#*\"current\": \"}
  b=${b%%\"*}
  if [[ $b == "$a" ]]; then
    echo "self-test FAILED: an application change kept the version"
    return 1
  fi
  if [[ $(versions_of "$work/b") != "$(printf '%s\n' "$a" "$b" | sort)" ]]; then
    echo "self-test FAILED: the site holds versions '$(versions_of "$work/b")', expected $a and $b"
    return 1
  fi
  if [[ $(<"$work/b/versions.json") != "{\"current\": \"$b\", \"previous\": \"$a\"}" ]]; then
    echo "self-test FAILED: versions.json holds $(<"$work/b/versions.json")"
    return 1
  fi
  for file in "${app_files[@]}"; do
    if ! cmp -s "$work/a/app/$a/$file" "$work/b/app/$a/$file"; then
      echo "self-test FAILED: the previous version's $file changed on the way"
      return 1
    fi
  done

  assembles "$work/app" "$work/database" "$work/a-again" "$url/a"
  if [[ $(versions_of "$work/a-again") != "$a" ]] || [[ $(<"$work/a-again/versions.json") != *'"previous": null}' ]]; then
    echo "self-test FAILED: a site over its own version holds '$(versions_of "$work/a-again")': $(<"$work/a-again/versions.json")"
    return 1
  fi
  cp -R "$work/b" "$served/b"
  assembles "$work/app-b" "$work/database" "$work/b-again" "$url/b"
  if [[ $(versions_of "$work/b-again") != "$(versions_of "$work/b")" ]] || [[ $(<"$work/b-again/versions.json") != "$(<"$work/b/versions.json")" ]]; then
    echo "self-test FAILED: a site over its own version with a previous one holds '$(versions_of "$work/b-again")': $(<"$work/b-again/versions.json")"
    return 1
  fi
  cp -R "$work/b" "$served/b-garbled"
  echo "{\"current\": \"$b\", \"previous\": \"x\"}" >"$served/b-garbled/versions.json"
  refuses "a site over its own version with a garbled previous one" "$work/app-b" "$work/database" "$work/x9" "$url/b-garbled"
  if ! grep -q 'names no previous version' "$out"; then
    echo "self-test FAILED: a garbled previous version was not named: $(<"$out")"
    return 1
  fi

  assembles "$work/app" "$work/database" "$work/first" "$url/nothing"
  if [[ $(<"$work/first/versions.json") != *'"previous": null}' ]]; then
    echo "self-test FAILED: a site with no live site before it holds $(<"$work/first/versions.json")"
    return 1
  fi

  cp -R "$work/a" "$served/incomplete"
  rm "$served/incomplete/app/$a/qtlogo.svg"
  refuses "a site over a live site that lacks a file of its version" "$work/app-b" "$work/database" "$work/x1" "$url/incomplete"
  if [[ -e $work/x1 ]] || ls -d "$work"/x1.* >/dev/null 2>&1; then
    echo "self-test FAILED: a failed assembly left $(ls -d "$work"/x1*) behind"
    return 1
  fi
  mkdir "$work/x10"
  refuses "a site over a live site that lacks a file of its version" "$work/app-b" "$work/database" "$work/x10" "$url/incomplete"
  if [[ ! -d $work/x10 ]] || [[ -n $(ls -A "$work/x10") ]] || ls -d "$work"/x10.* >/dev/null 2>&1; then
    echo "self-test FAILED: a failed assembly into an empty directory left $(ls -d "$work"/x10*) changed"
    return 1
  fi
  assembles "$work/app" "$work/database" "$work/x10"
  cp -R "$work/a" "$served/altered"
  printf '\0' >>"$served/altered/app/$a/qtloader.js"
  refuses "a site over a live site whose files do not match its version" "$work/app-b" "$work/database" "$work/x2" "$url/altered"
  mkdir "$served/garbled"
  echo 'not json' >"$served/garbled/versions.json"
  refuses "a site over a live site with a garbled versions.json" "$work/app-b" "$work/database" "$work/x3" "$url/garbled"
  refuses "a site over an unreachable live site" "$work/app-b" "$work/database" "$work/x4" "http://127.0.0.1:9"
  local status=0
  bash "$script" "$work/app" "$work/database" "$work/x8" "" >"$out" 2>&1 || status=$?
  if [[ $status != 3 ]] || [[ -e $work/x8 ]]; then
    echo "self-test FAILED: an empty previous site URL ended with $status, expected the usage and 3"
    return 1
  fi

  cp -R "$work/app" "$work/app-incomplete"
  rm "$work/app-incomplete/qtloader.js"
  refuses "a site without qtloader.js" "$work/app-incomplete" "$work/database" "$work/x5"
  refuses "a site into a directory that is not empty" "$work/app" "$work/database" "$work/a"
  sed -i.orig '2s/^/ /' "$web/coi-serviceworker.js"
  refuses "a site with an altered isolation worker" "$work/app" "$work/database" "$work/x6"
  if ! grep -q 'coi-serviceworker.js' "$out"; then
    echo "self-test FAILED: the altered isolation worker was not named: $(<"$out")"
    return 1
  fi
  rm "$web/coi-serviceworker.js"
  refuses "a site without the isolation worker" "$work/app" "$work/database" "$work/x7"
  if ! grep -q 'coi-serviceworker.js' "$out"; then
    echo "self-test FAILED: the missing isolation worker was not named: $(<"$out")"
    return 1
  fi
  echo "self-test passed"
}

case "${1:-}" in
  --self-test)
    self_test
    ;;
  *)
    if [[ $# != 3 && $# != 4 ]] || [[ $1 == -* ]] || [[ $# == 4 && -z $4 ]]; then
      echo "usage: $0 <app-dir> <database-dir> <site-dir> [<previous-site-url>] | --self-test" >&2
      exit 3
    fi
    assemble "$@"
    ;;
esac
