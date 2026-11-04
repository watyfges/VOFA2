#!/usr/bin/env bash
# Short/long soak harness for VOFA2.
#
#   MODE=demo ./scripts/soak.sh                 # built-in 4-channel demo source
#   MODE=tcp  ./scripts/soak.sh                 # 8ch JustFloat + corrupt bytes
#   SOAK_SECONDS=86400 MODE=tcp ./scripts/soak.sh   # full 24h soak
#
# Monitors RSS and fails if the app exits before the deadline or logs a crash.
set -euo pipefail

APP="${APP:-build-qt/src/app/vofa2_app}"
SECONDS_TO_RUN="${SOAK_SECONDS:-60}"
MODE="${MODE:-demo}"
PORT="${PORT:-23123}"
WORK_DIR="$(mktemp -d)"
export XDG_CONFIG_HOME="$WORK_DIR/config"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
mkdir -p "$XDG_CONFIG_HOME"

peer_pid=""
app_pid=""

cleanup() {
    if [ -n "$app_pid" ] && kill -0 "$app_pid" 2>/dev/null; then
        kill "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    if [ -n "$peer_pid" ] && kill -0 "$peer_pid" 2>/dev/null; then
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

app_args=(--demo)
if [ "$MODE" = "tcp" ]; then
    python3 -I "$(dirname "$0")/soak_peer.py" \
        --port "$PORT" --seconds "$((SECONDS_TO_RUN + 10))" \
        >"$WORK_DIR/peer.log" 2>&1 &
    peer_pid=$!
    sleep 0.5
    app_args=(--protocol JustFloat --tcp-client "127.0.0.1:$PORT")
fi

echo "soak: mode=$MODE seconds=$SECONDS_TO_RUN work=$WORK_DIR"
"$APP" "${app_args[@]}" >"$WORK_DIR/app.log" 2>&1 &
app_pid=$!

start=$(date +%s)
max_rss=0
while kill -0 "$app_pid" 2>/dev/null; do
    rss=$(awk '/VmRSS/{print $2}' "/proc/$app_pid/status" 2>/dev/null || echo 0)
    if [ -n "$rss" ] && [ "$rss" -gt "$max_rss" ]; then
        max_rss=$rss
    fi
    now=$(date +%s)
    if [ $((now - start)) -ge "$SECONDS_TO_RUN" ]; then
        break
    fi
    sleep 2
done

if ! kill -0 "$app_pid" 2>/dev/null; then
    echo "soak FAILED: app exited early" >&2
    cat "$WORK_DIR/app.log" >&2
    exit 1
fi

elapsed=$(($(date +%s) - start))
cleanup
app_pid=""

if grep -E "Sanitizer|Segmentation|ASSERT|terminate called" "$WORK_DIR/app.log" >/dev/null; then
    echo "soak FAILED: sanitizer/crash marker in app log" >&2
    cat "$WORK_DIR/app.log" >&2
    exit 1
fi

echo "soak PASSED: ${elapsed}s, max RSS ${max_rss} KiB"
if [ "$MODE" = "tcp" ]; then
    cat "$WORK_DIR/peer.log"
fi
