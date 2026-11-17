#!/bin/sh
# Diagnostic run of the normalized original 2.335 AX in elona_gui_host.
# The original install is only read (GameFileSystem game_root); saves/config go
# to a private user root under work/game-run/<name>. Not a playable launcher.
#
# Usage: tools/run_game_trace.sh <run-name> [extra host options...]
#   ELONA_BUILD=<build dir>        host build (default: legacy-cp932 debug)
#   REUSE_USER_FROM=<run-name>     start from a COPY of that run's user root
#
# Safety: an existing run directory is never deleted or reused (choose a new
# name). Run names must be plain [A-Za-z0-9._-] names, so neither the new run
# nor REUSE_USER_FROM can point outside work/game-run/.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME=${1:?run name}; shift
valid_name() {
  case "$1" in
    ''|.|..|*/*|*\\*) return 1 ;;
  esac
  printf '%s' "$1" | grep -Eq '^[A-Za-z0-9._-]+$'
}
valid_name "$NAME" || { echo "invalid run name: $NAME" >&2; exit 2; }
B=${ELONA_BUILD:-$ROOT/build/native-macos-arm64-legacy-cp932-debug}
RUNS=$ROOT/work/game-run
R=$RUNS/$NAME
if [ -e "$R" ] || [ -L "$R" ]; then
  echo "run directory already exists (refusing to overwrite earlier results): $R" >&2
  exit 2
fi
if [ -n "${REUSE_USER_FROM:-}" ]; then
  valid_name "$REUSE_USER_FROM" || { echo "invalid REUSE_USER_FROM: $REUSE_USER_FROM" >&2; exit 2; }
  SRC=$RUNS/$REUSE_USER_FROM/user
  [ -d "$SRC" ] && [ ! -L "$SRC" ] || { echo "no user root to reuse: $SRC" >&2; exit 2; }
fi
mkdir -p "$RUNS"
mkdir "$R"
mkdir "$R/cwd" "$R/user"
if [ -n "${REUSE_USER_FROM:-}" ]; then
  # Copy (never move or link): the earlier run's saves stay untouched.
  cp -pR "$SRC/." "$R/user/"
  echo "user root copied from $SRC" > "$R/user-origin.txt"
fi
( cd "$ROOT" && shasum -a 256 elonaplus2.335.zip elonaplus2.335/elonaplus.exe ) > "$R/original-before.sha256"
set +e
"$B/elona_gui_host" --game "$ROOT/work/normalized/2.335/start.ax" --game-root "$ROOT/elonaplus2.335" \
  --user-root "$R/user" --work-dir "$R/cwd" --report "$R/report.json" --screenshot "$R/screen0.bmp" \
  --trace-commands --packed-dir "$ROOT/work/extracted/2.335" "$@" > "$R/stdout.log" 2> "$R/stderr.log"
code=$?
set -e
( cd "$ROOT" && shasum -a 256 elonaplus2.335.zip elonaplus2.335/elonaplus.exe ) > "$R/original-after.sha256"
cmp -s "$R/original-before.sha256" "$R/original-after.sha256" && echo "original hashes unchanged" || echo "WARNING: original hashes changed"
echo "exit=$code run=$R"
tail -n 12 "$R/stderr.log"
exit "$code"
