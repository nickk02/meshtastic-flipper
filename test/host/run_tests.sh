#!/usr/bin/env bash
# Build and run the host tests. Needs gcc. Does not need a Flipper.
set -euo pipefail

cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
OUT=build
mkdir -p "$OUT"

# scoop does not always create a gcc shim, so fall back to the app directory
# rather than hardcoding a version that breaks on the next update.
if ! command -v gcc >/dev/null 2>&1; then
    for candidate in "$HOME"/scoop/apps/gcc/*/bin /c/Users/*/scoop/apps/gcc/*/bin; do
        if [ -x "$candidate/gcc.exe" ] || [ -x "$candidate/gcc" ]; then
            PATH="$candidate:$PATH"
            export PATH
            break
        fi
    done
fi

if ! command -v gcc >/dev/null 2>&1; then
    echo "gcc not found. Install it with: scoop install main/gcc" >&2
    exit 1
fi

CFLAGS="-std=c99 -Wall -Wextra -Werror -O1 -g"
INCLUDES="-I. -I$ROOT -I$ROOT/src/proto -I$ROOT/src/model -I$ROOT/lib/tiny-AES-c"

# tiny-AES-c selects its modes through #ifndef guards, so configure it here and
# leave the vendored sources byte-identical to upstream.
AES_DEFINES="-DCBC=0 -DECB=0 -DCTR=1"

PROTO_SRC=$(ls "$ROOT"/src/proto/*.c "$ROOT"/src/model/*.c "$ROOT"/src/radio/lora_config.c 2>/dev/null || true)
AES_SRC=$(ls "$ROOT"/lib/tiny-AES-c/aes.c 2>/dev/null || true)

status=0
for src in test_*.c; do
    name="${src%.c}"
    # shellcheck disable=SC2086
    gcc $CFLAGS $AES_DEFINES $INCLUDES -o "$OUT/$name.exe" "$src" $PROTO_SRC $AES_SRC -lm -lm
    echo "--- $name"
    if ! "./$OUT/$name.exe"; then
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo
    echo "SUITE FAILED"
fi
exit "$status"
