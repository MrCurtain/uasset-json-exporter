#!/usr/bin/env bash
#
# Wrapper for UAssetJsonExporter commandlets.
#
# UnrealEditor-Cmd.exe often stays alive long after a commandlet's Main has
# returned (shader compile workers, DDC commit, module shutdown). This wrapper
# watches the expected output JSON files, and once all are present AND their
# mtimes remain unchanged for IDLE_SEC seconds, force-kills the process.
#
# Usage:
#   run_commandlet.sh <UE_PATH> <UPROJECT> <RunName> <AssetList> [IDLE_SEC] [MAX_SEC]
#
# Args:
#   UE_PATH    Engine install root, e.g. "C:/Program Files/Epic Games/UE_5.7"
#   UPROJECT   Absolute path to the .uproject file
#   RunName    Commandlet name, e.g. "BlueprintEdGraphExport"
#   AssetList  Comma-separated asset paths, e.g. "/Game/Foo/BP_A,/Game/Bar/BP_B"
#   IDLE_SEC   Seconds of mtime stability before kill. Default 10.
#   MAX_SEC    Absolute upper bound on total wait. Default 600.
#
# Exit codes:
#   0 - all expected JSON files exist at end
#   1 - one or more expected JSON files missing
#   2 - bad invocation

set -u

if [ "$#" -lt 4 ]; then
    echo "Usage: $0 <UE_PATH> <UPROJECT> <RunName> <AssetList> [IDLE_SEC] [MAX_SEC]" >&2
    exit 2
fi

UE_PATH="$1"
UPROJECT="$2"
RUN="$3"
ASSETS="$4"
IDLE_SEC="${5:-10}"
MAX_SEC="${6:-600}"

UE_CMD="$UE_PATH/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJECT_DIR="$(dirname "$UPROJECT")"
EXPORT_ROOT="$PROJECT_DIR/Intermediate/UAssetExport"

if [ ! -x "$UE_CMD" ] && [ ! -f "$UE_CMD" ]; then
    echo "[run_commandlet] not found: $UE_CMD" >&2
    exit 2
fi
if [ ! -f "$UPROJECT" ]; then
    echo "[run_commandlet] not found: $UPROJECT" >&2
    exit 2
fi

# Expected outputs: /Game/Foo/Bar -> <ExportRoot>/Game/Foo/Bar.json
IFS=',' read -r -a ASSET_ARR <<< "$ASSETS"
EXPECTED_FILES=()
for A in "${ASSET_ARR[@]}"; do
    REL="${A#/}"
    OUT="$EXPORT_ROOT/$REL.json"
    EXPECTED_FILES+=("$OUT")
    rm -f "$OUT"
done

MSYS_NO_PATHCONV=1 "$UE_CMD" "$UPROJECT" \
    -run="$RUN" -assets="$ASSETS" \
    -nullrhi -nosplash -nosound -unattended \
    >/dev/null 2>&1 &
PID=$!
echo "[run_commandlet] launched PID=$PID run=$RUN idle=${IDLE_SEC}s max=${MAX_SEC}s" >&2

get_mtime() {
    stat -c %Y "$1" 2>/dev/null || echo 0
}

START_TS=$(date +%s)
STABLE_SINCE=0
LAST_SIG=""
KILL_REASON=""

while kill -0 "$PID" 2>/dev/null; do
    NOW=$(date +%s)

    if [ $((NOW - START_TS)) -gt "$MAX_SEC" ]; then
        KILL_REASON="MAX_SEC ${MAX_SEC}s exceeded"
        break
    fi

    SIG=""
    ALL_PRESENT=1
    for F in "${EXPECTED_FILES[@]}"; do
        if [ ! -f "$F" ]; then
            ALL_PRESENT=0
            break
        fi
        SIG="$SIG $(get_mtime "$F")"
    done

    if [ "$ALL_PRESENT" -eq 1 ]; then
        if [ "$SIG" = "$LAST_SIG" ] && [ "$STABLE_SINCE" -ne 0 ]; then
            if [ $((NOW - STABLE_SINCE)) -ge "$IDLE_SEC" ]; then
                KILL_REASON="outputs stable for ${IDLE_SEC}s"
                break
            fi
        else
            LAST_SIG="$SIG"
            STABLE_SINCE="$NOW"
        fi
    else
        STABLE_SINCE=0
        LAST_SIG=""
    fi

    sleep 1
done

if kill -0 "$PID" 2>/dev/null; then
    echo "[run_commandlet] killing PID=$PID reason=$KILL_REASON" >&2
    taskkill //F //PID "$PID" //T >/dev/null 2>&1
    wait "$PID" 2>/dev/null
else
    echo "[run_commandlet] PID=$PID exited on its own" >&2
fi

RC=0
for F in "${EXPECTED_FILES[@]}"; do
    if [ ! -f "$F" ]; then
        echo "[run_commandlet] missing output: $F" >&2
        RC=1
    fi
done

exit $RC
