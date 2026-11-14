#!/bin/bash
set -euo pipefail
# GNU grep in a UTF-8 locale skips a line that holds a byte outside UTF-8
export LC_ALL=C

baseline_file="$(dirname "$0")/blocking_calls.baseline"

blocking_call='QMessageBox::(information|warning|critical|about|aboutQt|question)\(|QtUtils::confirm\(|QInputDialog::get[A-Za-z]+\(|QFileDialog::get(OpenFile|SaveFile|ExistingDirectory)(Names?|Urls?)?\(|Q(Color|Font)Dialog::get[A-Za-z]+\(|setPopupMode\(|(^|[^A-Za-z0-9_])exec\('

count_blocking_calls() {
  local repo=$1
  git -C "$repo" ls-files -z -- 'cockatrice/src/*.cpp' 'cockatrice/src/*.h' 'libcockatrice_*/*.cpp' 'libcockatrice_*/*.h' |
    (cd "$repo" && xargs -0 grep -hE "$blocking_call" || true) |
    grep -vE '^[[:space:]]*(//|\*([[:space:]/]|$))' |
    grep -vcE '// blocking-ok: [^[:space:]].*$' || true
}

require_count() {
  local value=$1 holder=$2
  if [[ ! $value =~ ^(0|[1-9][0-9]*)$ ]]; then
    echo "::error::$holder holds '$value', not a count"
    return 1
  fi
}

check_against_baseline() {
  local count=$1 baseline=$2
  if ((count > baseline)); then
    echo "::error::$count blocking dialog calls, the baseline allows $baseline. Each one runs a nested event loop: use the Dialogs helpers in non_blocking_dialogs.h."
    return 1
  fi
  if ((count < baseline)); then
    local lower="$count blocking dialog calls, the baseline still says $baseline. Lower the baseline: bash $0 > $baseline_file"
    if [[ -n ${BLOCKING_CALLS_ALLOW_LOWER:-} ]]; then
      echo "::warning::$lower"
      return 0
    fi
    echo "::error::$lower"
    return 1
  fi
  echo "$count blocking dialog calls, as the baseline allows"
}

check_baseline_not_raised() {
  local base=$1 baseline=$2
  if ((baseline > base)); then
    echo "::error::The baseline allows $baseline blocking dialog calls, ${BLOCKING_CALLS_BASE_REF:-the base branch} allows $base. Remove the new blocking calls instead of raising the baseline."
    return 1
  fi
}

self_test() {
  local repo
  repo=$(mktemp -d)
  trap 'rm -rf "$repo"' RETURN
  mkdir -p "$repo/cockatrice/src" "$repo/libcockatrice_x/sub" "$repo/oracle/src"
  cat >"$repo/cockatrice/src/x.cpp" <<'EOF'
QMessageBox::information(this, "a", "b");
QMessageBox::warning(this, "a", "b");
QMessageBox::critical(this, "a", "b");
QMessageBox::about(this, "a", "b");
QMessageBox::aboutQt(this);
QMessageBox::question(this, "a", "b");
if (QtUtils::confirm(this, "a")) {
auto s = QInputDialog::getText(this, "a", "b");
auto f = QFileDialog::getOpenFileName(this);
auto g = QFileDialog::getOpenFileNames(this);
auto h = QFileDialog::getSaveFileName(this);
auto i = QFileDialog::getExistingDirectory(this);
auto j = QFileDialog::getOpenFileUrl(this);
auto k = QFileDialog::getSaveFileUrl(this);
auto c = QColorDialog::getColor(Qt::red, this);
auto t = QFontDialog::getFont(&ok, this);
button->setPopupMode(QToolButton::InstantPopup);
split->setPopupMode(QToolButton::MenuButtonPopup);
delayed->setPopupMode(QToolButton::DelayedPopup);
setPopupMode(InstantPopup);
tool->setPopupMode( mode );
if (exec() == QDialog::Accepted) {
return QDialog::exec();
    *ok = dialog->exec() == QDialog::Accepted;
QFileDialog::getOpenFileContent("*", cb);
executeQuery(); reexec(1);
return app.exec(); // blocking-ok: the application loop
    // QMessageBox::warning(this, "a", "b");
     * QMessageBox::warning(this, "a", "b");
EOF
  printf 'QMessageBox::warning(this, "caf\351");\n' >>"$repo/cockatrice/src/x.cpp"
  printf 'dlg.exec();\nmenu->exec(pos);\n' >"$repo/libcockatrice_x/sub/y.h"
  echo 'dlg.exec();' >"$repo/oracle/src/z.cpp"
  echo 'dlg.exec();' >"$repo/cockatrice/src/notes.txt"
  git init -q "$repo"
  git -C "$repo" add -A

  local count
  count=$(count_blocking_calls "$repo")
  if [[ $count != 27 ]]; then
    echo "self-test FAILED: counted $count blocking calls in the fixture, expected 27"
    return 1
  fi
  if check_against_baseline 7 6 >/dev/null; then
    echo "self-test FAILED: a count above the baseline passed the check"
    return 1
  fi
  if BLOCKING_CALLS_ALLOW_LOWER='' check_against_baseline 5 6 >/dev/null; then
    echo "self-test FAILED: a count below the baseline passed the check"
    return 1
  fi
  if ! BLOCKING_CALLS_ALLOW_LOWER=1 check_against_baseline 5 6 >/dev/null; then
    echo "self-test FAILED: a count below the baseline failed the check that allows it"
    return 1
  fi
  if BLOCKING_CALLS_ALLOW_LOWER=1 check_against_baseline 7 6 >/dev/null; then
    echo "self-test FAILED: a count above the baseline passed the check that allows a lower one"
    return 1
  fi
  local not_a_count
  for not_a_count in '' 381x 08 $'381\r' '<<<<<<< HEAD'; do
    if require_count "$not_a_count" fixture >/dev/null; then
      echo "self-test FAILED: '$not_a_count' passed as a count"
      return 1
    fi
  done
  if ! require_count 0 fixture >/dev/null || ! require_count 381 fixture >/dev/null; then
    echo "self-test FAILED: a count did not pass as one"
    return 1
  fi
  if ! check_against_baseline 6 6 >/dev/null; then
    echo "self-test FAILED: a count equal to the baseline failed the check"
    return 1
  fi
  if check_baseline_not_raised 6 7 >/dev/null; then
    echo "self-test FAILED: a baseline above the base branch's passed the check"
    return 1
  fi
  if ! check_baseline_not_raised 6 6 >/dev/null || ! check_baseline_not_raised 6 5 >/dev/null; then
    echo "self-test FAILED: a baseline not above the base branch's failed the check"
    return 1
  fi
  echo "self-test passed"
}

case "${1:-}" in
  --self-test)
    self_test
    ;;
  --check)
    top=$(git rev-parse --show-toplevel)
    baseline=$(<"$baseline_file")
    require_count "$baseline" "$baseline_file"
    check_against_baseline "$(count_blocking_calls "$top")" "$baseline"
    if [[ -n ${BLOCKING_CALLS_BASE_REF:-} ]]; then
      # A depth on a full clone would cut its history down to one commit
      if [[ $(git -C "$top" rev-parse --is-shallow-repository) == true ]]; then
        git -C "$top" fetch --quiet --depth 1 origin "$BLOCKING_CALLS_BASE_REF"
      else
        git -C "$top" fetch --quiet origin "$BLOCKING_CALLS_BASE_REF"
      fi
      if base=$(git -C "$top" show FETCH_HEAD:.ci/blocking_calls.baseline 2>/dev/null); then
        require_count "$base" "$BLOCKING_CALLS_BASE_REF:.ci/blocking_calls.baseline"
        check_baseline_not_raised "$base" "$baseline"
      fi
    fi
    ;;
  '')
    count_blocking_calls "$(git rev-parse --show-toplevel)"
    ;;
  *)
    echo "usage: $0 [--check | --self-test]" >&2
    exit 3
    ;;
esac
