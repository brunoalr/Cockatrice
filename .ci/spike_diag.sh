#!/bin/bash
QTDIR=$(find "$GITHUB_WORKSPACE/Qt" -depth -maxdepth 2 -name macos -type d -print -quit)
for t in libexec/moc libexec/uic libexec/rcc; do echo "$t archs: $(lipo -archs "$QTDIR/$t")"; done
echo "cockatrice archs: $(lipo -archs build/cockatrice/cockatrice.app/Contents/MacOS/cockatrice)"
inc="-I$QTDIR/include"
for m in QtCore QtGui QtWidgets QtNetwork QtWebSockets QtMultimedia QtSvg QtConcurrent QtSql QtXml; do inc="$inc -I$QTDIR/include/$m"; done
for d in lib*/ cockatrice oracle servatrice; do inc="$inc -I${d%/}"; done
grep -rlE 'Q_OBJECT|Q_GADGET|Q_NAMESPACE' --include='*.h' cockatrice lib* oracle servatrice >"$RUNNER_TEMP/headers.txt" || true
mkdir -p "$RUNNER_TEMP/moccmp"
n=0
diffs=0
nonzero=0
tx=0
ta=0
while read -r h; do
  n=$((n + 1))
  s0=$(date +%s)
  arch -x86_64 "$QTDIR/libexec/moc" $inc "$h" -o "$RUNNER_TEMP/moccmp/x.cpp" 2>/dev/null
  rx=$?
  s1=$(date +%s)
  arch -arm64 "$QTDIR/libexec/moc" $inc "$h" -o "$RUNNER_TEMP/moccmp/a.cpp" 2>/dev/null
  ra=$?
  s2=$(date +%s)
  tx=$((tx + s1 - s0))
  ta=$((ta + s2 - s1))
  if [[ $rx -ne $ra ]] || ! cmp -s "$RUNNER_TEMP/moccmp/x.cpp" "$RUNNER_TEMP/moccmp/a.cpp"; then
    diffs=$((diffs + 1))
    echo "DIFF $h"
  fi
  if [[ $rx -ne 0 ]]; then nonzero=$((nonzero + 1)); fi
done <"$RUNNER_TEMP/headers.txt"
echo "MOCCMP headers=$n differing=$diffs moc-nonzero-exit=$nonzero x86_64-secs=$tx arm64-secs=$ta"
echo "::group::slowest ninja edges"
awk -F'\t' 'NR>1 {print ($2-$1)/1000 "\t" $4}' build/.ninja_log | sort -rn | head -15
echo "::endgroup::"
awk -F'\t' 'NR>1 && $4 ~ /_autogen\/timestamp/ {d=($2-$1)/1000; s+=d; if (d>m) {m=d; w=$4}} END {printf "NINJA autogen edges: total %.1f s, max %.1f s (%s)\n", s, m, w}' build/.ninja_log
awk -F'\t' 'NR>1 && $4 ~ /qrc_|\/ui_|mocs_compilation/ {printf "NINJA %s %.1f s\n", $4, ($2-$1)/1000}' build/.ninja_log | head -20
