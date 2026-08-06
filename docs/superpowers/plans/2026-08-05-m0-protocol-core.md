# M0 Protocol Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and prove the entire Meshtastic receive-decode path (channel key
expansion, AES-CTR decryption, packet header parsing, protobuf payload
decoding) on a PC, with no radio hardware present.

**Architecture:** Everything in `src/proto/` compiles under plain gcc with zero
Flipper dependencies. It takes byte buffers in and plain structs out and
allocates nothing. Tests run on the PC against vectors generated independently
in Python via OpenSSL and the real Meshtastic protobuf definitions. A separate
task installs ufbt and measures real FAP headroom on the device.

**Tech Stack:** C99, gcc (mingw, already on PATH), a dependency-free assert
harness, Python 3 with `cryptography` and `meshtastic` for vector generation,
tiny-AES-c (public domain) vendored for the cipher core, ufbt for the FAP build.

## Global Constraints

Copied from `docs/superpowers/specs/2026-08-05-meshtastic-flipper-design.md`.

- **No em-dashes** in any code, comment, commit message, or documentation.
- **No AI attribution anywhere.** No `Co-Authored-By`, no generated-with
  footers, no AI mentions in commits, PR bodies, READMEs or comments.
- Commits authored as `nickk02 <98576999+nickk02@users.noreply.github.com>`,
  imperative mood, 50-character subject.
- **`src/proto/` must not include a single Flipper header.** No `furi.h`, no
  `furi_hal_*.h`. If a task tempts you to add one, the design is wrong.
- **No dynamic allocation** anywhere in `src/proto/`. No `malloc`, no `calloc`.
  Callers supply all buffers.
- **No invented constants.** Every protocol constant traces to a cited file and
  line in the checked-out Meshtastic source, or to the Semtech datasheet.
  Reference checkouts live in `.local-scratch/vendor/` and are gitignored.
- Third party code keeps its original license header and lives in `vendor/`.
- This plan covers **M0 only**. M1 onward stay as spec outline until the
  hardware arrives and M0's results are in.

## Reference checkouts

These already exist from the design phase. If missing, re-clone:

```bash
cd .local-scratch/vendor
git clone --filter=blob:none --sparse --depth=1 https://github.com/meshtastic/firmware.git
cd firmware && git sparse-checkout set src/mesh && cd ..
git clone --depth=1 https://github.com/meshtastic/protobufs.git
```

## File Structure

| File | Responsibility |
| --- | --- |
| `vendor/tiny-AES-c/aes.c`, `aes.h` | Public domain AES128 core, CTR mode. Unmodified except config. |
| `vendor/tiny-AES-c/aes_config.h` | Our build config: AES128 on, CBC/ECB off. |
| `src/proto/mesh_channel.h/.c` | `defaultpsk` bytes, short-PSK expansion, channel hash. |
| `src/proto/mesh_crypto.h/.c` | Nonce construction, AES-CTR wrapper over tiny-AES-c. |
| `src/proto/mesh_header.h/.c` | 16-byte `PacketHeader` parse, flag bit accessors. |
| `src/proto/mesh_data.h/.c` | Protobuf field walker for `Data`: portnum and payload only. |
| `src/proto/mesh_decode.h/.c` | Top-level frame to result pipeline, and the failure reason enum. |
| `test/host/tinytest.h` | Assert macros and a test runner. No dependencies. |
| `test/host/test_*.c` | One test file per `src/proto` unit. |
| `test/host/vectors.h` | Generated. Do not hand-edit. |
| `test/host/run_tests.sh` | Compiles and runs everything with gcc. No `make` dependency. |
| `test/tools/gen_vectors.py` | Generates `vectors.h` independently in Python. |

`run_tests.sh` rather than a Makefile because Git Bash on this machine has no
`make`, only mingw's `gcc`.

---

### Task 1: ufbt toolchain and headroom measurement

Independent of every other task. Needs the Flipper connected by USB. If the
device is not to hand, skip to Task 2 and return here later. Nothing else
blocks on it.

**Files:**
- Create: `application.fam`
- Create: `meshtastic_flipper.c`
- Create: `docs/measurements.md`

**Interfaces:**
- Consumes: nothing.
- Produces: a recorded FAP size and free-heap number in `docs/measurements.md`,
  consumed by the M3 size report, not by code.

- [ ] **Step 1: Install ufbt and record the version**

```bash
python -m pip install --upgrade ufbt
ufbt update
ufbt --version
```

Expected: a version prints without error. If `ufbt` is not found afterwards,
its scripts directory is not on PATH. Invoke it as `python -m ufbt` instead and
note that in `docs/measurements.md`.

- [ ] **Step 2: Record the pinned SDK version**

```bash
ufbt status
```

Expected: prints the SDK target and firmware version. Copy the exact version
string. This is the pinned target for the whole project. Every later FuriHAL
signature gets verified against this version, not against a guess.

- [ ] **Step 3: Write the minimal FAP manifest**

Create `application.fam`:

```python
App(
    appid="meshtastic_flipper",
    name="Meshtastic",
    apptype=FlipperAppType.EXTERNAL,
    entry_point="meshtastic_flipper_app",
    stack_size=2 * 1024,
    fap_category="Tools",
)
```

- [ ] **Step 4: Write the minimal app that reports free heap**

Create `meshtastic_flipper.c`:

```c
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

typedef struct {
    size_t heap_free;
    size_t heap_total;
} MeasureState;

static void measure_draw(Canvas* canvas, void* ctx) {
    MeasureState* state = ctx;
    char line[48];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Meshtastic M0");

    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Heap free: %u", (unsigned)state->heap_free);
    canvas_draw_str(canvas, 2, 28, line);
    snprintf(line, sizeof(line), "Heap total: %u", (unsigned)state->heap_total);
    canvas_draw_str(canvas, 2, 40, line);
    canvas_draw_str(canvas, 2, 56, "Back to exit");
}

static void measure_input(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

int32_t meshtastic_flipper_app(void* p) {
    UNUSED(p);

    MeasureState state = {
        .heap_free = memmgr_get_free_heap(),
        .heap_total = memmgr_get_total_heap(),
    };

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, measure_draw, &state);
    view_port_input_callback_set(view_port, measure_input, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    while(furi_message_queue_get(queue, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.type == InputTypeShort && event.key == InputKeyBack) break;
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    return 0;
}
```

- [ ] **Step 5: Build it**

```bash
ufbt
```

Expected: build succeeds and reports the `.fap` size. If `memmgr_get_free_heap`
or `memmgr_get_total_heap` fails to link, grep the pinned SDK for the correct
name rather than guessing:

```bash
grep -rn "get_free_heap\|get_total_heap" ~/.ufbt/current/sdk_headers/
```

- [ ] **Step 6: Launch on the device and read the numbers**

Connect the Flipper by USB, close qFlipper if it is running, then:

```bash
ufbt launch
```

Expected: the app opens on the Flipper and shows two numbers.

- [ ] **Step 7: Record the measurements**

Create `docs/measurements.md` with the real values observed, for example:

```markdown
# Measurements

## M0, 2026-08-05

- ufbt SDK target: <exact string from `ufbt status`>
- Empty FAP size: <bytes from the ufbt build output>
- Free heap with app running: <number from the device screen>
- Total heap: <number from the device screen>

Free heap is the practical ceiling for everything the app allocates at runtime.
FAP size is code plus static data and is separate from it.
```

- [ ] **Step 8: Commit**

```bash
git add application.fam meshtastic_flipper.c docs/measurements.md
git commit -m "Add minimal FAP and record device headroom"
```

---

### Task 2: Host test harness

**Files:**
- Create: `test/host/tinytest.h`
- Create: `test/host/run_tests.sh`
- Create: `test/host/test_harness.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `TEST(name)`, `RUN_TEST(name)`, `ASSERT_TRUE(cond)`,
  `ASSERT_EQ_INT(a, b)`, `ASSERT_EQ_MEM(a, b, len)`, `TEST_MAIN_BEGIN()`,
  `TEST_MAIN_END()`. Every later test file uses these exact names.

- [ ] **Step 1: Confirm gcc is present**

```bash
gcc --version
```

Expected: a version banner. If not found, add mingw to PATH:
`export PATH="/c/Users/Nick/scoop/apps/mingw/current/bin:$PATH"` and record
that in `test/host/run_tests.sh`.

- [ ] **Step 2: Write the test harness header**

Create `test/host/tinytest.h`:

```c
#ifndef TINYTEST_H
#define TINYTEST_H

#include <stdio.h>
#include <string.h>

static int tt_failures = 0;
static int tt_checks = 0;
static const char* tt_current = "";

#define TEST(name) static void name(void)

#define RUN_TEST(name)         \
    do {                       \
        tt_current = #name;    \
        name();                \
    } while(0)

#define TT_FAIL(fmt, ...)                                                    \
    do {                                                                     \
        tt_failures++;                                                       \
        printf("FAIL %s (%s:%d): " fmt "\n", tt_current, __FILE__, __LINE__, \
               ##__VA_ARGS__);                                               \
    } while(0)

#define ASSERT_TRUE(cond)              \
    do {                               \
        tt_checks++;                   \
        if(!(cond)) TT_FAIL("%s", #cond); \
    } while(0)

#define ASSERT_EQ_INT(a, b)                                        \
    do {                                                           \
        tt_checks++;                                               \
        long long tt_a = (long long)(a);                           \
        long long tt_b = (long long)(b);                           \
        if(tt_a != tt_b) TT_FAIL("%s: %lld != %lld", #a, tt_a, tt_b); \
    } while(0)

static void tt_dump(const char* label, const unsigned char* p, size_t n) {
    printf("  %s:", label);
    for(size_t i = 0; i < n; i++) printf(" %02x", p[i]);
    printf("\n");
}

#define ASSERT_EQ_MEM(a, b, len)                             \
    do {                                                     \
        tt_checks++;                                         \
        if(memcmp((a), (b), (len)) != 0) {                   \
            TT_FAIL("%s != %s over %d bytes", #a, #b, (int)(len)); \
            tt_dump("got     ", (const unsigned char*)(a), (len)); \
            tt_dump("expected", (const unsigned char*)(b), (len)); \
        }                                                    \
    } while(0)

#define TEST_MAIN_BEGIN() int main(void) {

#define TEST_MAIN_END()                                              \
    printf("%s: %d checks, %d failures\n",                           \
           tt_failures ? "FAILED" : "PASSED", tt_checks, tt_failures); \
    return tt_failures ? 1 : 0;                                      \
    }

#endif
```

- [ ] **Step 3: Write a test that proves the harness detects failure**

Create `test/host/test_harness.c`:

```c
#include "tinytest.h"

TEST(test_assert_true_passes) {
    ASSERT_TRUE(1 == 1);
}

TEST(test_assert_eq_int_passes) {
    ASSERT_EQ_INT(2 + 2, 4);
}

TEST(test_assert_eq_mem_passes) {
    const unsigned char a[3] = {1, 2, 3};
    const unsigned char b[3] = {1, 2, 3};
    ASSERT_EQ_MEM(a, b, 3);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_assert_true_passes);
RUN_TEST(test_assert_eq_int_passes);
RUN_TEST(test_assert_eq_mem_passes);
TEST_MAIN_END()
```

- [ ] **Step 4: Write the runner script**

Create `test/host/run_tests.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
OUT=build
mkdir -p "$OUT"

CFLAGS="-std=c99 -Wall -Wextra -Werror -O1 -g"
INCLUDES="-I. -I$ROOT/src/proto -I$ROOT/vendor/tiny-AES-c"

status=0
for src in test_*.c; do
    name="${src%.c}"
    # Compile the test together with every proto source that exists so far.
    # shellcheck disable=SC2086
    gcc $CFLAGS $INCLUDES -o "$OUT/$name.exe" "$src" \
        $(ls "$ROOT"/src/proto/*.c 2>/dev/null || true) \
        $(ls "$ROOT"/vendor/tiny-AES-c/aes.c 2>/dev/null || true)
    echo "--- $name"
    if ! "./$OUT/$name.exe"; then
        status=1
    fi
done

exit $status
```

- [ ] **Step 5: Run it and verify it passes**

```bash
bash test/host/run_tests.sh
```

Expected output ends with:

```
--- test_harness
PASSED: 3 checks, 0 failures
```

- [ ] **Step 6: Prove the harness actually catches failures**

Temporarily change `ASSERT_EQ_INT(2 + 2, 4)` to `ASSERT_EQ_INT(2 + 2, 5)` and
re-run.

Expected: a `FAIL` line naming `test_assert_eq_int_passes`, a final line
starting `FAILED`, and a non-zero exit code. A harness that cannot fail is
worthless, so confirm this before trusting any later test. Revert the change
afterwards and re-run to confirm `PASSED`.

- [ ] **Step 7: Commit**

```bash
git add test/host/tinytest.h test/host/test_harness.c test/host/run_tests.sh
git commit -m "Add dependency-free host test harness"
```

---

### Task 3: Vendor tiny-AES-c and prove it against OpenSSL

**Files:**
- Create: `vendor/tiny-AES-c/aes.c`, `vendor/tiny-AES-c/aes.h`, `vendor/tiny-AES-c/LICENSE`
- Create: `vendor/tiny-AES-c/README-vendoring.md`
- Create: `test/host/test_aes.c`

**Interfaces:**
- Consumes: `tinytest.h` from Task 2.
- Produces: `AES_ctx`, `AES_init_ctx_iv(struct AES_ctx*, const uint8_t* key, const uint8_t* iv)`,
  `AES_CTR_xcrypt_buffer(struct AES_ctx*, uint8_t* buf, size_t length)`.
  Task 5 calls exactly these.

- [ ] **Step 1: Vendor the library**

```bash
mkdir -p vendor
cd .local-scratch
git clone --depth=1 https://github.com/kokke/tiny-AES-c.git
cd ..
cp .local-scratch/tiny-AES-c/aes.c .local-scratch/tiny-AES-c/aes.h \
   .local-scratch/tiny-AES-c/LICENSE vendor/tiny-AES-c/
```

Create `vendor/tiny-AES-c/README-vendoring.md`:

```markdown
# tiny-AES-c

Vendored from https://github.com/kokke/tiny-AES-c
Commit: <record the exact SHA from `git -C .local-scratch/tiny-AES-c rev-parse HEAD`>
License: public domain (see LICENSE)

Used for the AES128 block cipher and CTR mode only. Configured in `aes.h` with
AES128 enabled and CBC and ECB disabled, because Meshtastic's channel
encryption is AES-CTR only.

Its `AES_CTR_xcrypt_buffer` increments the whole 16-byte counter block as a
big-endian integer. Meshtastic uses `setCounterSize(4)`, meaning only the low 4
bytes increment. These are identical for any payload short enough that the low
4 bytes never overflow, which at 255 bytes maximum (16 blocks) is always. Do
not "fix" this difference.
```

- [ ] **Step 2: Configure it for AES128 and CTR only**

Edit `vendor/tiny-AES-c/aes.h` so the configuration block reads:

```c
#define CBC 0
#define ECB 0
#define CTR 1

#define AES128 1
//#define AES192 1
//#define AES256 1
```

- [ ] **Step 3: Write the failing known-answer test**

Create `test/host/test_aes.c`. The expected ciphertext is deliberately wrong so
the test fails first and proves it is really running.

```c
#include "tinytest.h"
#include "aes.h"

/* NIST SP 800-38A F.5.1, CTR-AES128.Encrypt.
   Cross-checked against OpenSSL in Task 4's generator. */
TEST(test_aes128_ctr_known_answer) {
    const uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                             0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    const uint8_t iv[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                            0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
    uint8_t buf[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
                       0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
    const uint8_t expected[16] = {0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
                                  0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce};

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, buf, sizeof(buf));

    ASSERT_EQ_MEM(buf, expected, 16);
}

/* CTR is its own inverse. Encrypting twice must return the original. */
TEST(test_aes128_ctr_roundtrip) {
    const uint8_t key[16] = {0};
    const uint8_t iv[16] = {0};
    const uint8_t original[20] = "hello meshtastic!!!";
    uint8_t buf[20];
    struct AES_ctx ctx;

    memcpy(buf, original, sizeof(buf));

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, buf, sizeof(buf));
    ASSERT_TRUE(memcmp(buf, original, sizeof(buf)) != 0);

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, buf, sizeof(buf));
    ASSERT_EQ_MEM(buf, original, sizeof(buf));
}

TEST_MAIN_BEGIN()
RUN_TEST(test_aes128_ctr_known_answer);
RUN_TEST(test_aes128_ctr_roundtrip);
TEST_MAIN_END()
```

- [ ] **Step 4: Run and verify both pass**

```bash
bash test/host/run_tests.sh
```

Expected: `PASSED: 2 checks, 0 failures` for `test_aes`.

If `test_aes128_ctr_known_answer` fails, the hardcoded NIST vector above was
mistyped. Do not adjust the expected bytes to match the output. Instead confirm
the correct values from NIST SP 800-38A appendix F.5.1, or wait for Task 4's
generator, which derives the same vector from OpenSSL independently. A
known-answer test edited to match its own output tests nothing.

- [ ] **Step 5: Commit**

```bash
git add vendor/tiny-AES-c test/host/test_aes.c
git commit -m "Vendor tiny-AES-c and add AES128-CTR known answer test"
```

---

### Task 4: Independent vector generator

**Files:**
- Create: `test/tools/gen_vectors.py`
- Create: `test/tools/requirements.txt`
- Create (generated): `test/host/vectors.h`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `test/host/vectors.h` defining, for each of N cases,
  `VEC<n>_KEY[16]`, `VEC<n>_PACKET_ID` (uint32), `VEC<n>_FROM_NODE` (uint32),
  `VEC<n>_NONCE[16]`, `VEC<n>_PLAINTEXT[]`, `VEC<n>_PLAINTEXT_LEN`,
  `VEC<n>_CIPHERTEXT[]`, `VEC<n>_CIPHERTEXT_LEN`, `VEC<n>_TEXT[]`,
  `VEC<n>_PORTNUM`, `VEC<n>_FRAME[]`, `VEC<n>_FRAME_LEN`. Tasks 5 through 9
  read these names.

- [ ] **Step 1: Confirm the portnum value from source rather than memory**

```bash
grep -n "TEXT_MESSAGE_APP" .local-scratch/vendor/protobufs/meshtastic/portnums.proto
```

Expected: a line assigning `TEXT_MESSAGE_APP` a number. Record that number. Use
it below. Do not assume it.

- [ ] **Step 2: Install the Python dependencies**

Create `test/tools/requirements.txt`:

```
cryptography>=42.0
meshtastic>=2.3
```

```bash
python -m pip install -r test/tools/requirements.txt
```

`cryptography` is OpenSSL-backed, so it is a genuinely independent AES
implementation from tiny-AES-c. `meshtastic` ships the real generated protobuf
classes, so the `Data` encoding is ground truth rather than our own reading of
the `.proto`.

- [ ] **Step 3: Write the generator**

Create `test/tools/gen_vectors.py`:

```python
#!/usr/bin/env python3
"""Generate host test vectors for the Meshtastic receive path.

Independence is the point. AES comes from OpenSSL via `cryptography`, and the
Data protobuf comes from the real meshtastic package, so a shared
misunderstanding in the C code cannot quietly pass.

Run from the repo root:
    python test/tools/gen_vectors.py > test/host/vectors.h
"""

import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

try:
    from meshtastic.protobuf import mesh_pb2, portnums_pb2
except ImportError:  # older meshtastic layout
    from meshtastic import mesh_pb2, portnums_pb2

# Channels.h:153-154 in meshtastic/firmware.
DEFAULT_PSK = bytes([
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01,
])


def expand_psk(index: int) -> bytes:
    """Channels.cpp:254-267. Index 1 means defaultpsk unchanged."""
    if index == 0:
        raise ValueError("index 0 means encryption disabled")
    key = bytearray(DEFAULT_PSK)
    key[-1] = (key[-1] + index - 1) & 0xFF
    return bytes(key)


def build_nonce(packet_id: int, from_node: int) -> bytes:
    """CryptoEngine::initNonce.

    8 bytes packet id little endian, 4 bytes source node little endian, then a
    4 byte counter starting at zero.
    """
    return struct.pack("<QII", packet_id, from_node, 0)


def aes_ctr(key: bytes, nonce: bytes, data: bytes) -> bytes:
    cipher = Cipher(algorithms.AES(key), modes.CTR(nonce))
    enc = cipher.encryptor()
    return enc.update(data) + enc.finalize()


def build_data(text: str) -> bytes:
    data = mesh_pb2.Data()
    data.portnum = portnums_pb2.PortNum.TEXT_MESSAGE_APP
    data.payload = text.encode("utf-8")
    return data.SerializeToString()


def xor_hash(b: bytes) -> int:
    h = 0
    for x in b:
        h ^= x
    return h


def channel_hash(name: str, key: bytes) -> int:
    """Channels::getHash. xorHash(name) XOR xorHash(key)."""
    return xor_hash(name.encode("utf-8")) ^ xor_hash(key)


def build_header(to, frm, packet_id, flags, chan_hash, next_hop, relay):
    """RadioInterface.h:36-53. Exactly 16 bytes."""
    return struct.pack("<IIIBBBB", to, frm, packet_id, flags, chan_hash,
                       next_hop, relay)


def c_bytes(name, data):
    body = ", ".join("0x%02x" % b for b in data)
    return "static const uint8_t %s[] = {%s};\n" % (name, body)


CASES = [
    # (label, text, to, from, packet_id, hop_limit, hop_start, psk_index, chan_name)
    ("simple",  "hello mesh",      0xFFFFFFFF, 0x11223344, 0x0A0B0C0D, 3, 3, 1, "LongFast"),
    ("empty",   "",                0xFFFFFFFF, 0x00000001, 0x00000001, 0, 0, 1, "LongFast"),
    ("long",    "x" * 180,         0xFFFFFFFF, 0xDEADBEEF, 0xCAFEBABE, 7, 7, 1, "LongFast"),
    ("psk2",    "second key",      0xFFFFFFFF, 0x0000ABCD, 0x00001234, 2, 3, 2, "LongFast"),
    ("utf8",    "café ✓", 0xFFFFFFFF, 0x55667788, 0x99AABBCC, 3, 3, 1, "LongFast"),
]


def main():
    out = sys.stdout
    out.write("/* Generated by test/tools/gen_vectors.py. Do not hand-edit. */\n")
    out.write("#ifndef VECTORS_H\n#define VECTORS_H\n\n")
    out.write("#include <stdint.h>\n\n")
    out.write("#define VECTOR_COUNT %d\n\n" % len(CASES))

    for i, (label, text, to, frm, pid, hop_limit, hop_start, psk_index,
            chan_name) in enumerate(CASES):
        key = expand_psk(psk_index)
        nonce = build_nonce(pid, frm)
        plaintext = build_data(text)
        ciphertext = aes_ctr(key, nonce, plaintext)
        chash = channel_hash(chan_name, key)
        flags = (hop_limit & 0x07) | ((hop_start & 0x07) << 5)
        header = build_header(to, frm, pid, flags, chash, 0, 0)
        frame = header + ciphertext

        p = "VEC%d_" % i
        out.write("/* case %d: %s */\n" % (i, label))
        out.write(c_bytes(p + "KEY", key))
        out.write("#define %sPACKET_ID 0x%08xu\n" % (p, pid))
        out.write("#define %sFROM_NODE 0x%08xu\n" % (p, frm))
        out.write("#define %sTO_NODE 0x%08xu\n" % (p, to))
        out.write("#define %sFLAGS 0x%02xu\n" % (p, flags))
        out.write("#define %sHOP_LIMIT %d\n" % (p, hop_limit))
        out.write("#define %sHOP_START %d\n" % (p, hop_start))
        out.write("#define %sCHANNEL_HASH 0x%02xu\n" % (p, chash))
        out.write("#define %sPORTNUM %d\n" % (p, portnums_pb2.PortNum.TEXT_MESSAGE_APP))
        out.write(c_bytes(p + "NONCE", nonce))
        out.write(c_bytes(p + "PLAINTEXT", plaintext))
        out.write("#define %sPLAINTEXT_LEN %d\n" % (p, len(plaintext)))
        out.write(c_bytes(p + "CIPHERTEXT", ciphertext))
        out.write("#define %sCIPHERTEXT_LEN %d\n" % (p, len(ciphertext)))
        out.write(c_bytes(p + "FRAME", frame))
        out.write("#define %sFRAME_LEN %d\n" % (p, len(frame)))
        out.write(c_bytes(p + "TEXT", text.encode("utf-8")))
        out.write("#define %sTEXT_LEN %d\n\n" % (p, len(text.encode("utf-8"))))

    out.write("#endif\n")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Generate the header and eyeball it**

```bash
python test/tools/gen_vectors.py > test/host/vectors.h
head -20 test/host/vectors.h
```

Expected: a valid C header, `VECTOR_COUNT 5`, and `VEC0_KEY` whose bytes are
`d4 f1 bb 3a 20 29 07 59 f0 bc ff ab cf 4e 69 01`. If `VEC0_KEY` differs from
`defaultpsk`, `expand_psk` is wrong for index 1.

- [ ] **Step 5: Sanity check the generator against itself**

```bash
python -c "
import sys; sys.path.insert(0, 'test/tools')
from gen_vectors import aes_ctr, expand_psk, build_nonce
k = expand_psk(1); n = build_nonce(0x0A0B0C0D, 0x11223344)
pt = b'roundtrip check'
assert aes_ctr(k, n, aes_ctr(k, n, pt)) == pt
print('generator roundtrip ok')
"
```

Expected: `generator roundtrip ok`.

- [ ] **Step 6: Commit**

```bash
git add test/tools/gen_vectors.py test/tools/requirements.txt test/host/vectors.h
git commit -m "Add independent test vector generator"
```

`vectors.h` is committed rather than generated at build time so the tests run
without Python installed and so changes to the vectors show up in diffs.

---

### Task 5: Channel key expansion and channel hash

**Files:**
- Create: `src/proto/mesh_channel.h`, `src/proto/mesh_channel.c`
- Create: `test/host/test_channel.c`

**Interfaces:**
- Consumes: `vectors.h` from Task 4, `tinytest.h` from Task 2.
- Produces:
  - `#define MESH_PSK_LEN 16`
  - `extern const uint8_t mesh_default_psk[MESH_PSK_LEN];`
  - `bool mesh_channel_expand_psk(uint8_t index, uint8_t out[MESH_PSK_LEN]);`
  - `uint8_t mesh_channel_xor_hash(const uint8_t* p, size_t len);`
  - `uint8_t mesh_channel_hash(const char* name, const uint8_t* key, size_t key_len);`

  Task 9 calls `mesh_channel_expand_psk` and `mesh_channel_hash`.

- [ ] **Step 1: Write the failing test**

Create `test/host/test_channel.c`:

```c
#include "tinytest.h"
#include "mesh_channel.h"
#include "vectors.h"

TEST(test_default_psk_matches_source) {
    const uint8_t expected[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                  0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
    ASSERT_EQ_MEM(mesh_default_psk, expected, MESH_PSK_LEN);
}

TEST(test_expand_psk_index_1_is_verbatim) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_MEM(key, VEC0_KEY, MESH_PSK_LEN);
}

TEST(test_expand_psk_index_2_bumps_last_byte) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(2, key));
    ASSERT_EQ_MEM(key, VEC3_KEY, MESH_PSK_LEN);
    ASSERT_EQ_INT(key[MESH_PSK_LEN - 1], mesh_default_psk[MESH_PSK_LEN - 1] + 1);
}

TEST(test_expand_psk_index_0_is_rejected) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(!mesh_channel_expand_psk(0, key));
}

TEST(test_expand_psk_wraps_without_overflow) {
    /* Last defaultpsk byte is 0x01, so index 256 would wrap to 0x00.
       Index is a uint8_t so the largest is 255, giving 0x01 + 254 = 0xFF. */
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(255, key));
    ASSERT_EQ_INT(key[MESH_PSK_LEN - 1], 0xFF);
}

TEST(test_xor_hash_folds_bytes) {
    const uint8_t data[4] = {0x0F, 0xF0, 0x00, 0xFF};
    ASSERT_EQ_INT(mesh_channel_xor_hash(data, 4), 0x00);
}

TEST(test_channel_hash_matches_generator) {
    uint8_t key[MESH_PSK_LEN];
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(mesh_channel_hash("LongFast", key, MESH_PSK_LEN),
                  VEC0_CHANNEL_HASH);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_default_psk_matches_source);
RUN_TEST(test_expand_psk_index_1_is_verbatim);
RUN_TEST(test_expand_psk_index_2_bumps_last_byte);
RUN_TEST(test_expand_psk_index_0_is_rejected);
RUN_TEST(test_expand_psk_wraps_without_overflow);
RUN_TEST(test_xor_hash_folds_bytes);
RUN_TEST(test_channel_hash_matches_generator);
TEST_MAIN_END()
```

- [ ] **Step 2: Run and verify it fails to compile**

```bash
bash test/host/run_tests.sh
```

Expected: FAIL with `mesh_channel.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/proto/mesh_channel.h`:

```c
#ifndef MESH_CHANNEL_H
#define MESH_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Meshtastic default channel PSK is AES128. Channels.h:153-154. */
#define MESH_PSK_LEN 16

extern const uint8_t mesh_default_psk[MESH_PSK_LEN];

/* Expand a short single byte PSK into full key bytes. Channels.cpp:254-267.
   Index 0 means encryption disabled and is rejected here. Index 1 yields
   mesh_default_psk unchanged. Higher indices bump the last byte by index - 1.
   Returns false and leaves out untouched if index is 0. */
bool mesh_channel_expand_psk(uint8_t index, uint8_t out[MESH_PSK_LEN]);

/* Byte-wise XOR fold. Channels.cpp xorHash. */
uint8_t mesh_channel_xor_hash(const uint8_t* p, size_t len);

/* Channel hash carried in the header's channel byte as a decode hint.
   xorHash(name) XOR xorHash(key). Channels::getHash. */
uint8_t mesh_channel_hash(const char* name, const uint8_t* key, size_t key_len);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/proto/mesh_channel.c`:

```c
#include "mesh_channel.h"

#include <string.h>

const uint8_t mesh_default_psk[MESH_PSK_LEN] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01,
};

bool mesh_channel_expand_psk(uint8_t index, uint8_t out[MESH_PSK_LEN]) {
    if(index == 0) return false;

    memcpy(out, mesh_default_psk, MESH_PSK_LEN);
    out[MESH_PSK_LEN - 1] = (uint8_t)(out[MESH_PSK_LEN - 1] + index - 1);
    return true;
}

uint8_t mesh_channel_xor_hash(const uint8_t* p, size_t len) {
    uint8_t code = 0;
    for(size_t i = 0; i < len; i++) code ^= p[i];
    return code;
}

uint8_t mesh_channel_hash(const char* name, const uint8_t* key, size_t key_len) {
    uint8_t h = mesh_channel_xor_hash((const uint8_t*)name, strlen(name));
    h ^= mesh_channel_xor_hash(key, key_len);
    return h;
}
```

- [ ] **Step 5: Run and verify all seven pass**

```bash
bash test/host/run_tests.sh
```

Expected: `PASSED: 9 checks, 0 failures` for `test_channel`.

- [ ] **Step 6: Commit**

```bash
git add src/proto/mesh_channel.h src/proto/mesh_channel.c test/host/test_channel.c
git commit -m "Add channel key expansion and channel hash"
```

---

### Task 6: Nonce construction and AES-CTR decryption

**Files:**
- Create: `src/proto/mesh_crypto.h`, `src/proto/mesh_crypto.c`
- Create: `test/host/test_crypto.c`

**Interfaces:**
- Consumes: `AES_ctx`, `AES_init_ctx_iv`, `AES_CTR_xcrypt_buffer` from Task 3.
  `MESH_PSK_LEN` from Task 5. `vectors.h` from Task 4.
- Produces:
  - `#define MESH_NONCE_LEN 16`
  - `void mesh_crypto_build_nonce(uint32_t packet_id, uint32_t from_node, uint8_t nonce[MESH_NONCE_LEN]);`
  - `void mesh_crypto_xcrypt(const uint8_t key[MESH_PSK_LEN], uint32_t packet_id, uint32_t from_node, const uint8_t* in, size_t len, uint8_t* out);`

  Task 9 calls `mesh_crypto_xcrypt`.

- [ ] **Step 1: Write the failing test**

Create `test/host/test_crypto.c`:

```c
#include "tinytest.h"
#include "mesh_channel.h"
#include "mesh_crypto.h"
#include "vectors.h"

TEST(test_nonce_layout_matches_generator) {
    uint8_t nonce[MESH_NONCE_LEN];
    mesh_crypto_build_nonce(VEC0_PACKET_ID, VEC0_FROM_NODE, nonce);
    ASSERT_EQ_MEM(nonce, VEC0_NONCE, MESH_NONCE_LEN);
}

TEST(test_nonce_bytes_4_to_7_are_zero) {
    /* Packet id is a 32 bit value widened to 64, so the high half is zero. */
    uint8_t nonce[MESH_NONCE_LEN];
    mesh_crypto_build_nonce(0xFFFFFFFFu, 0xFFFFFFFFu, nonce);
    for(int i = 4; i < 8; i++) ASSERT_EQ_INT(nonce[i], 0);
    for(int i = 12; i < 16; i++) ASSERT_EQ_INT(nonce[i], 0);
}

TEST(test_decrypt_recovers_plaintext) {
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN, out);
    ASSERT_EQ_MEM(out, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN);
}

TEST(test_decrypt_long_payload_spans_blocks) {
    /* 180 characters of text, so well over one AES block. Catches a counter
       that fails to advance between blocks. */
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC2_KEY, VEC2_PACKET_ID, VEC2_FROM_NODE,
                       VEC2_CIPHERTEXT, VEC2_CIPHERTEXT_LEN, out);
    ASSERT_EQ_MEM(out, VEC2_PLAINTEXT, VEC2_PLAINTEXT_LEN);
}

TEST(test_decrypt_with_second_psk) {
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC3_KEY, VEC3_PACKET_ID, VEC3_FROM_NODE,
                       VEC3_CIPHERTEXT, VEC3_CIPHERTEXT_LEN, out);
    ASSERT_EQ_MEM(out, VEC3_PLAINTEXT, VEC3_PLAINTEXT_LEN);
}

TEST(test_wrong_key_does_not_recover) {
    uint8_t bad_key[MESH_PSK_LEN];
    uint8_t out[256];
    memcpy(bad_key, VEC0_KEY, MESH_PSK_LEN);
    bad_key[0] ^= 0xFF;

    mesh_crypto_xcrypt(bad_key, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN, out);
    ASSERT_TRUE(memcmp(out, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN) != 0);
}

TEST(test_wrong_nonce_does_not_recover) {
    uint8_t out[256];
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID + 1, VEC0_FROM_NODE,
                       VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN, out);
    ASSERT_TRUE(memcmp(out, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN) != 0);
}

TEST(test_xcrypt_is_its_own_inverse) {
    uint8_t once[256];
    uint8_t twice[256];
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN, once);
    mesh_crypto_xcrypt(VEC0_KEY, VEC0_PACKET_ID, VEC0_FROM_NODE,
                       once, VEC0_PLAINTEXT_LEN, twice);
    ASSERT_EQ_MEM(twice, VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN);
    ASSERT_EQ_MEM(once, VEC0_CIPHERTEXT, VEC0_CIPHERTEXT_LEN);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_nonce_layout_matches_generator);
RUN_TEST(test_nonce_bytes_4_to_7_are_zero);
RUN_TEST(test_decrypt_recovers_plaintext);
RUN_TEST(test_decrypt_long_payload_spans_blocks);
RUN_TEST(test_decrypt_with_second_psk);
RUN_TEST(test_wrong_key_does_not_recover);
RUN_TEST(test_wrong_nonce_does_not_recover);
RUN_TEST(test_xcrypt_is_its_own_inverse);
TEST_MAIN_END()
```

- [ ] **Step 2: Run and verify it fails to compile**

```bash
bash test/host/run_tests.sh
```

Expected: FAIL with `mesh_crypto.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/proto/mesh_crypto.h`:

```c
#ifndef MESH_CRYPTO_H
#define MESH_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include "mesh_channel.h"

/* AES-CTR IV width. CryptoEngine::encryptAESCtr uses setIV(nonce, 16). */
#define MESH_NONCE_LEN 16

/* Build the per-packet nonce. CryptoEngine::initNonce.
   Bytes 0-7:   packet id, little endian, widened from 32 to 64 bits.
   Bytes 8-11:  source node number, little endian.
   Bytes 12-15: block counter, starts at zero. */
void mesh_crypto_build_nonce(
    uint32_t packet_id,
    uint32_t from_node,
    uint8_t nonce[MESH_NONCE_LEN]);

/* AES128-CTR over len bytes. CTR is symmetric, so this both encrypts and
   decrypts. in and out may not overlap. out must hold at least len bytes. */
void mesh_crypto_xcrypt(
    const uint8_t key[MESH_PSK_LEN],
    uint32_t packet_id,
    uint32_t from_node,
    const uint8_t* in,
    size_t len,
    uint8_t* out);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/proto/mesh_crypto.c`:

```c
#include "mesh_crypto.h"

#include <string.h>

#include "aes.h"

void mesh_crypto_build_nonce(
    uint32_t packet_id,
    uint32_t from_node,
    uint8_t nonce[MESH_NONCE_LEN]) {
    memset(nonce, 0, MESH_NONCE_LEN);

    /* Written byte-wise rather than by memcpy of a uint32_t so the layout is
       little endian regardless of host byte order. */
    nonce[0] = (uint8_t)(packet_id & 0xFF);
    nonce[1] = (uint8_t)((packet_id >> 8) & 0xFF);
    nonce[2] = (uint8_t)((packet_id >> 16) & 0xFF);
    nonce[3] = (uint8_t)((packet_id >> 24) & 0xFF);

    /* Bytes 4-7 stay zero: the packet id is 32 bits widened to 64. */

    nonce[8] = (uint8_t)(from_node & 0xFF);
    nonce[9] = (uint8_t)((from_node >> 8) & 0xFF);
    nonce[10] = (uint8_t)((from_node >> 16) & 0xFF);
    nonce[11] = (uint8_t)((from_node >> 24) & 0xFF);

    /* Bytes 12-15 are the block counter and start at zero. */
}

void mesh_crypto_xcrypt(
    const uint8_t key[MESH_PSK_LEN],
    uint32_t packet_id,
    uint32_t from_node,
    const uint8_t* in,
    size_t len,
    uint8_t* out) {
    uint8_t nonce[MESH_NONCE_LEN];
    struct AES_ctx ctx;

    mesh_crypto_build_nonce(packet_id, from_node, nonce);

    /* tiny-AES-c works in place, so copy first. */
    memcpy(out, in, len);

    AES_init_ctx_iv(&ctx, key, nonce);
    AES_CTR_xcrypt_buffer(&ctx, out, len);
}
```

- [ ] **Step 5: Run and verify all eight pass**

```bash
bash test/host/run_tests.sh
```

Expected: `PASSED: 15 checks, 0 failures` for `test_crypto`.

If `test_nonce_layout_matches_generator` fails, compare the C nonce against
`VEC0_NONCE` in the dump the harness prints. A mismatch in bytes 8 through 11
means the source node is in the wrong position. This is the M0 kill condition
in the spec, so if the layout cannot be reconciled with
`CryptoEngine::initNonce`, stop and report rather than adjusting the test.

- [ ] **Step 6: Commit**

```bash
git add src/proto/mesh_crypto.h src/proto/mesh_crypto.c test/host/test_crypto.c
git commit -m "Add packet nonce construction and AES-CTR"
```

---

### Task 7: Packet header parsing

**Files:**
- Create: `src/proto/mesh_header.h`, `src/proto/mesh_header.c`
- Create: `test/host/test_header.c`

**Interfaces:**
- Consumes: `vectors.h` from Task 4, `tinytest.h` from Task 2.
- Produces:
  - `#define MESH_HEADER_LEN 16`
  - `#define MESH_MAX_PAYLOAD 255`
  - `typedef struct { uint32_t to; uint32_t from; uint32_t id; uint8_t flags; uint8_t channel; uint8_t next_hop; uint8_t relay_node; } MeshHeader;`
  - `bool mesh_header_parse(const uint8_t* buf, size_t len, MeshHeader* out);`
  - `uint8_t mesh_header_hop_limit(const MeshHeader* h);`
  - `uint8_t mesh_header_hop_start(const MeshHeader* h);`
  - `bool mesh_header_want_ack(const MeshHeader* h);`
  - `bool mesh_header_via_mqtt(const MeshHeader* h);`

  Task 9 calls `mesh_header_parse` and reads every struct field.

- [ ] **Step 1: Write the failing test**

Create `test/host/test_header.c`:

```c
#include "tinytest.h"
#include "mesh_header.h"
#include "vectors.h"

TEST(test_header_is_16_bytes) {
    ASSERT_EQ_INT(MESH_HEADER_LEN, 16);
}

TEST(test_parse_fields) {
    MeshHeader h;
    ASSERT_TRUE(mesh_header_parse(VEC0_FRAME, VEC0_FRAME_LEN, &h));
    ASSERT_EQ_INT(h.to, VEC0_TO_NODE);
    ASSERT_EQ_INT(h.from, VEC0_FROM_NODE);
    ASSERT_EQ_INT(h.id, VEC0_PACKET_ID);
    ASSERT_EQ_INT(h.flags, VEC0_FLAGS);
    ASSERT_EQ_INT(h.channel, VEC0_CHANNEL_HASH);
    ASSERT_EQ_INT(h.next_hop, 0);
    ASSERT_EQ_INT(h.relay_node, 0);
}

TEST(test_flag_accessors) {
    MeshHeader h;
    ASSERT_TRUE(mesh_header_parse(VEC0_FRAME, VEC0_FRAME_LEN, &h));
    ASSERT_EQ_INT(mesh_header_hop_limit(&h), VEC0_HOP_LIMIT);
    ASSERT_EQ_INT(mesh_header_hop_start(&h), VEC0_HOP_START);
}

TEST(test_flag_bits_decode_independently) {
    /* hop limit 5 (0x05), want_ack set (0x08), via_mqtt set (0x10),
       hop start 7 (0x07 << 5 = 0xE0). RadioInterface.h:24-28. */
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    frame[12] = 0x05 | 0x08 | 0x10 | 0xE0;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(mesh_header_hop_limit(&h), 5);
    ASSERT_EQ_INT(mesh_header_hop_start(&h), 7);
    ASSERT_TRUE(mesh_header_want_ack(&h));
    ASSERT_TRUE(mesh_header_via_mqtt(&h));
}

TEST(test_flag_bits_clear) {
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    frame[12] = 0x00;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(mesh_header_hop_limit(&h), 0);
    ASSERT_EQ_INT(mesh_header_hop_start(&h), 0);
    ASSERT_TRUE(!mesh_header_want_ack(&h));
    ASSERT_TRUE(!mesh_header_via_mqtt(&h));
}

TEST(test_parse_rejects_short_buffer) {
    uint8_t frame[MESH_HEADER_LEN - 1] = {0};
    MeshHeader h;
    ASSERT_TRUE(!mesh_header_parse(frame, sizeof(frame), &h));
}

TEST(test_parse_accepts_exactly_header_length) {
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
}

TEST(test_parse_rejects_null) {
    MeshHeader h;
    ASSERT_TRUE(!mesh_header_parse(NULL, MESH_HEADER_LEN, &h));
}

TEST(test_little_endian_decode) {
    /* to = 0x04030201 stored as 01 02 03 04. Catches a byte order flip. */
    uint8_t frame[MESH_HEADER_LEN] = {0};
    MeshHeader h;
    frame[0] = 0x01; frame[1] = 0x02; frame[2] = 0x03; frame[3] = 0x04;

    ASSERT_TRUE(mesh_header_parse(frame, MESH_HEADER_LEN, &h));
    ASSERT_EQ_INT(h.to, 0x04030201u);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_header_is_16_bytes);
RUN_TEST(test_parse_fields);
RUN_TEST(test_flag_accessors);
RUN_TEST(test_flag_bits_decode_independently);
RUN_TEST(test_flag_bits_clear);
RUN_TEST(test_parse_rejects_short_buffer);
RUN_TEST(test_parse_accepts_exactly_header_length);
RUN_TEST(test_parse_rejects_null);
RUN_TEST(test_little_endian_decode);
TEST_MAIN_END()
```

- [ ] **Step 2: Run and verify it fails to compile**

```bash
bash test/host/run_tests.sh
```

Expected: FAIL with `mesh_header.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/proto/mesh_header.h`:

```c
#ifndef MESH_HEADER_H
#define MESH_HEADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RadioInterface.h:20-21. */
#define MESH_HEADER_LEN 16
#define MESH_MAX_PAYLOAD 255

/* RadioInterface.h:24-28. */
#define MESH_FLAG_HOP_LIMIT_MASK 0x07
#define MESH_FLAG_WANT_ACK_MASK 0x08
#define MESH_FLAG_VIA_MQTT_MASK 0x10
#define MESH_FLAG_HOP_START_MASK 0xE0
#define MESH_FLAG_HOP_START_SHIFT 5

/* Wire layout of PacketHeader. RadioInterface.h:36-53. All multi-byte fields
   are little endian on the wire. */
typedef struct {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t flags;
    uint8_t channel; /* channel hash, a decode hint */
    uint8_t next_hop;
    uint8_t relay_node;
} MeshHeader;

/* Returns false if buf is NULL or len is under MESH_HEADER_LEN. */
bool mesh_header_parse(const uint8_t* buf, size_t len, MeshHeader* out);

uint8_t mesh_header_hop_limit(const MeshHeader* h);
uint8_t mesh_header_hop_start(const MeshHeader* h);
bool mesh_header_want_ack(const MeshHeader* h);
bool mesh_header_via_mqtt(const MeshHeader* h);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/proto/mesh_header.c`:

```c
#include "mesh_header.h"

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool mesh_header_parse(const uint8_t* buf, size_t len, MeshHeader* out) {
    if(buf == NULL || out == NULL) return false;
    if(len < MESH_HEADER_LEN) return false;

    out->to = read_u32_le(buf + 0);
    out->from = read_u32_le(buf + 4);
    out->id = read_u32_le(buf + 8);
    out->flags = buf[12];
    out->channel = buf[13];
    out->next_hop = buf[14];
    out->relay_node = buf[15];
    return true;
}

uint8_t mesh_header_hop_limit(const MeshHeader* h) {
    return (uint8_t)(h->flags & MESH_FLAG_HOP_LIMIT_MASK);
}

uint8_t mesh_header_hop_start(const MeshHeader* h) {
    return (uint8_t)((h->flags & MESH_FLAG_HOP_START_MASK) >>
                     MESH_FLAG_HOP_START_SHIFT);
}

bool mesh_header_want_ack(const MeshHeader* h) {
    return (h->flags & MESH_FLAG_WANT_ACK_MASK) != 0;
}

bool mesh_header_via_mqtt(const MeshHeader* h) {
    return (h->flags & MESH_FLAG_VIA_MQTT_MASK) != 0;
}
```

- [ ] **Step 5: Run and verify all nine pass**

```bash
bash test/host/run_tests.sh
```

Expected: `PASSED: 21 checks, 0 failures` for `test_header`.

- [ ] **Step 6: Commit**

```bash
git add src/proto/mesh_header.h src/proto/mesh_header.c test/host/test_header.c
git commit -m "Add Meshtastic packet header parsing"
```

---

### Task 8: Protobuf field walker for Data

**Files:**
- Create: `src/proto/mesh_data.h`, `src/proto/mesh_data.c`
- Create: `test/host/test_data.c`

**Interfaces:**
- Consumes: `vectors.h` from Task 4, `tinytest.h` from Task 2.
- Produces:
  - `#define MESH_PORTNUM_TEXT_MESSAGE_APP <value confirmed in Task 4 Step 1>`
  - `typedef struct { uint32_t portnum; const uint8_t* payload; size_t payload_len; } MeshData;`
  - `bool mesh_data_parse(const uint8_t* buf, size_t len, MeshData* out);`

  Task 9 calls `mesh_data_parse`.

`payload` points into the caller's buffer. It does not copy and does not
allocate. The caller must keep the decrypted buffer alive while using it.

- [ ] **Step 1: Write the failing test**

Create `test/host/test_data.c`:

```c
#include "tinytest.h"
#include "mesh_data.h"
#include "vectors.h"

TEST(test_parse_simple_message) {
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC0_PLAINTEXT, VEC0_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.portnum, VEC0_PORTNUM);
    ASSERT_EQ_INT(d.payload_len, VEC0_TEXT_LEN);
    ASSERT_EQ_MEM(d.payload, VEC0_TEXT, VEC0_TEXT_LEN);
}

TEST(test_parse_empty_payload) {
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC1_PLAINTEXT, VEC1_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.payload_len, 0);
}

TEST(test_parse_long_payload_multibyte_length) {
    /* 180 bytes, so the length prefix is a two byte varint. Catches a parser
       that assumes single byte lengths. */
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC2_PLAINTEXT, VEC2_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.payload_len, VEC2_TEXT_LEN);
    ASSERT_EQ_MEM(d.payload, VEC2_TEXT, VEC2_TEXT_LEN);
}

TEST(test_parse_utf8_payload_is_byte_exact) {
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(VEC4_PLAINTEXT, VEC4_PLAINTEXT_LEN, &d));
    ASSERT_EQ_INT(d.payload_len, VEC4_TEXT_LEN);
    ASSERT_EQ_MEM(d.payload, VEC4_TEXT, VEC4_TEXT_LEN);
}

TEST(test_skips_unknown_varint_field) {
    /* field 1 varint = 1, field 3 varint = 1 (want_response, unknown to us),
       field 2 bytes = "hi". The unknown field must be skipped, not fatal. */
    const uint8_t buf[] = {0x08, 0x01, 0x18, 0x01, 0x12, 0x02, 'h', 'i'};
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(buf, sizeof(buf), &d));
    ASSERT_EQ_INT(d.portnum, 1);
    ASSERT_EQ_INT(d.payload_len, 2);
    ASSERT_EQ_MEM(d.payload, "hi", 2);
}

TEST(test_skips_unknown_fixed32_field) {
    /* field 5 fixed32 (wire type 5), then field 2 bytes. */
    const uint8_t buf[] = {0x2d, 0xAA, 0xBB, 0xCC, 0xDD, 0x12, 0x02, 'o', 'k'};
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(buf, sizeof(buf), &d));
    ASSERT_EQ_INT(d.payload_len, 2);
    ASSERT_EQ_MEM(d.payload, "ok", 2);
}

TEST(test_skips_unknown_fixed64_field) {
    /* field 5 fixed64 (wire type 1), then field 2 bytes. */
    const uint8_t buf[] = {0x29, 1, 2, 3, 4, 5, 6, 7, 8, 0x12, 0x02, 'o', 'k'};
    MeshData d;
    ASSERT_TRUE(mesh_data_parse(buf, sizeof(buf), &d));
    ASSERT_EQ_INT(d.payload_len, 2);
    ASSERT_EQ_MEM(d.payload, "ok", 2);
}

TEST(test_rejects_truncated_varint) {
    /* Continuation bit set on the final byte. */
    const uint8_t buf[] = {0x08, 0x80};
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_length_past_end) {
    /* field 2 claims 99 bytes but only 2 follow. Garbage from a bad decrypt
       looks exactly like this, so it must not read out of bounds. */
    const uint8_t buf[] = {0x12, 0x63, 'a', 'b'};
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_unsupported_wire_type) {
    /* Wire type 6 is not valid in protobuf. */
    const uint8_t buf[] = {0x0e, 0x01};
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(buf, sizeof(buf), &d));
}

TEST(test_rejects_null_and_zero_length) {
    MeshData d;
    ASSERT_TRUE(!mesh_data_parse(NULL, 4, &d));
    /* Zero length is a valid but empty message: no portnum, no payload. */
    ASSERT_TRUE(mesh_data_parse((const uint8_t*)"", 0, &d));
    ASSERT_EQ_INT(d.portnum, 0);
    ASSERT_EQ_INT(d.payload_len, 0);
}

TEST(test_random_bytes_usually_rejected) {
    /* A wrong key produces effectively random plaintext. Most such buffers
       must be rejected rather than yielding a bogus message. */
    uint8_t buf[64];
    MeshData d;
    int accepted = 0;
    for(int seed = 0; seed < 200; seed++) {
        uint32_t x = (uint32_t)seed * 2654435761u;
        for(size_t i = 0; i < sizeof(buf); i++) {
            x = x * 1103515245u + 12345u;
            buf[i] = (uint8_t)(x >> 16);
        }
        if(mesh_data_parse(buf, sizeof(buf), &d)) accepted++;
    }
    printf("  random buffers accepted: %d of 200\n", accepted);
    ASSERT_TRUE(accepted < 40);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_parse_simple_message);
RUN_TEST(test_parse_empty_payload);
RUN_TEST(test_parse_long_payload_multibyte_length);
RUN_TEST(test_parse_utf8_payload_is_byte_exact);
RUN_TEST(test_skips_unknown_varint_field);
RUN_TEST(test_skips_unknown_fixed32_field);
RUN_TEST(test_skips_unknown_fixed64_field);
RUN_TEST(test_rejects_truncated_varint);
RUN_TEST(test_rejects_length_past_end);
RUN_TEST(test_rejects_unsupported_wire_type);
RUN_TEST(test_rejects_null_and_zero_length);
RUN_TEST(test_random_bytes_usually_rejected);
TEST_MAIN_END()
```

- [ ] **Step 2: Run and verify it fails to compile**

```bash
bash test/host/run_tests.sh
```

Expected: FAIL with `mesh_data.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Replace `<PORTNUM>` with the number confirmed in Task 4 Step 1.

Create `src/proto/mesh_data.h`:

```c
#ifndef MESH_DATA_H
#define MESH_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Confirmed from protobufs/meshtastic/portnums.proto. */
#define MESH_PORTNUM_TEXT_MESSAGE_APP <PORTNUM>

/* Decoded subset of the Data message. Only the two fields this project reads
   are extracted. Everything else is skipped by wire type.

   payload points into the caller's buffer. Nothing is copied or allocated, so
   the source buffer must outlive this struct. */
typedef struct {
    uint32_t portnum;
    const uint8_t* payload;
    size_t payload_len;
} MeshData;

/* Returns false on malformed input. Because a failed decryption yields
   effectively random bytes, rejecting malformed input is a load-bearing
   behaviour, not just hygiene. */
bool mesh_data_parse(const uint8_t* buf, size_t len, MeshData* out);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/proto/mesh_data.c`:

```c
#include "mesh_data.h"

#define WIRE_VARINT 0
#define WIRE_FIXED64 1
#define WIRE_LEN 2
#define WIRE_FIXED32 5

#define FIELD_PORTNUM 1
#define FIELD_PAYLOAD 2

/* Reads a base 128 varint. Advances *pos. Returns false if the buffer ends
   mid-varint or the value exceeds 64 bits. */
static bool read_varint(const uint8_t* buf, size_t len, size_t* pos, uint64_t* out) {
    uint64_t value = 0;
    unsigned shift = 0;

    while(*pos < len) {
        uint8_t byte = buf[*pos];
        (*pos)++;

        if(shift >= 64) return false;
        value |= (uint64_t)(byte & 0x7F) << shift;

        if((byte & 0x80) == 0) {
            *out = value;
            return true;
        }
        shift += 7;
    }
    return false; /* ran off the end mid-varint */
}

bool mesh_data_parse(const uint8_t* buf, size_t len, MeshData* out) {
    if(out == NULL) return false;
    if(buf == NULL && len > 0) return false;

    out->portnum = 0;
    out->payload = NULL;
    out->payload_len = 0;

    size_t pos = 0;
    while(pos < len) {
        uint64_t tag;
        if(!read_varint(buf, len, &pos, &tag)) return false;

        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wire = (uint8_t)(tag & 0x07);

        if(field == 0) return false;

        switch(wire) {
        case WIRE_VARINT: {
            uint64_t value;
            if(!read_varint(buf, len, &pos, &value)) return false;
            if(field == FIELD_PORTNUM) out->portnum = (uint32_t)value;
            break;
        }
        case WIRE_LEN: {
            uint64_t size;
            if(!read_varint(buf, len, &pos, &size)) return false;
            if(size > (uint64_t)(len - pos)) return false;
            if(field == FIELD_PAYLOAD) {
                out->payload = buf + pos;
                out->payload_len = (size_t)size;
            }
            pos += (size_t)size;
            break;
        }
        case WIRE_FIXED32:
            if(len - pos < 4) return false;
            pos += 4;
            break;
        case WIRE_FIXED64:
            if(len - pos < 8) return false;
            pos += 8;
            break;
        default:
            return false; /* wire types 3, 4 and 6 are not expected here */
        }
    }
    return true;
}
```

- [ ] **Step 5: Run and verify all twelve pass**

```bash
bash test/host/run_tests.sh
```

Expected: `PASSED` for `test_data`, and a printed line reporting how many of
200 random buffers were accepted. That number is informational. If it exceeds
40 the parser is too permissive and will surface bogus messages at M3.

- [ ] **Step 6: Commit**

```bash
git add src/proto/mesh_data.h src/proto/mesh_data.c test/host/test_data.c
git commit -m "Add protobuf field walker for Data messages"
```

---

### Task 9: End-to-end decode pipeline, M0 acceptance

**Files:**
- Create: `src/proto/mesh_decode.h`, `src/proto/mesh_decode.c`
- Create: `test/host/test_decode.c`

**Interfaces:**
- Consumes: everything from Tasks 5 through 8.
- Produces:
  - `typedef enum { MESH_OK, MESH_ERR_TOO_SHORT, MESH_ERR_CHANNEL_MISMATCH, MESH_ERR_BAD_PROTOBUF, MESH_ERR_NOT_TEXT } MeshDecodeResult;`
  - `typedef struct { MeshHeader header; MeshData data; uint8_t plaintext[MESH_MAX_PAYLOAD]; size_t plaintext_len; } MeshDecoded;`
  - `MeshDecodeResult mesh_decode_frame(const uint8_t* frame, size_t len, const uint8_t key[MESH_PSK_LEN], uint8_t expected_channel_hash, MeshDecoded* out);`
  - `const char* mesh_decode_result_name(MeshDecodeResult r);`

  M3's `radio_thread` calls `mesh_decode_frame` and stores the result in
  `MeshFrame`. The enum is the failure-reason set the spec's counters view
  displays.

- [ ] **Step 1: Write the failing test**

Create `test/host/test_decode.c`:

```c
#include "tinytest.h"
#include "mesh_channel.h"
#include "mesh_decode.h"
#include "vectors.h"

/* The M0 acceptance test from the spec: a synthesized encrypted frame must
   produce the exact original text, byte for byte. */
TEST(test_acceptance_frame_to_text) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;

    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key, VEC0_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_INT(d.data.portnum, MESH_PORTNUM_TEXT_MESSAGE_APP);
    ASSERT_EQ_INT(d.data.payload_len, VEC0_TEXT_LEN);
    ASSERT_EQ_MEM(d.data.payload, VEC0_TEXT, VEC0_TEXT_LEN);
    ASSERT_EQ_INT(d.header.from, VEC0_FROM_NODE);
}

TEST(test_acceptance_long_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC2_FRAME, VEC2_FRAME_LEN, key, VEC2_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_MEM(d.data.payload, VEC2_TEXT, VEC2_TEXT_LEN);
}

TEST(test_acceptance_utf8_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC4_FRAME, VEC4_FRAME_LEN, key, VEC4_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_MEM(d.data.payload, VEC4_TEXT, VEC4_TEXT_LEN);
}

TEST(test_acceptance_second_psk_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(2, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC3_FRAME, VEC3_FRAME_LEN, key, VEC3_CHANNEL_HASH, &d),
        MESH_OK);
    ASSERT_EQ_MEM(d.data.payload, VEC3_TEXT, VEC3_TEXT_LEN);
}

TEST(test_rejects_short_frame) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    uint8_t frame[8] = {0};
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(mesh_decode_frame(frame, sizeof(frame), key, 0, &d),
                  MESH_ERR_TOO_SHORT);
}

TEST(test_rejects_channel_hash_mismatch) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    ASSERT_EQ_INT(
        mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key,
                          (uint8_t)(VEC0_CHANNEL_HASH ^ 0xFF), &d),
        MESH_ERR_CHANNEL_MISMATCH);
}

TEST(test_wrong_key_reports_bad_protobuf_not_ok) {
    uint8_t key[MESH_PSK_LEN];
    MeshDecoded d;
    MeshDecodeResult r;

    ASSERT_TRUE(mesh_channel_expand_psk(1, key));
    key[0] ^= 0xFF;

    r = mesh_decode_frame(VEC0_FRAME, VEC0_FRAME_LEN, key, VEC0_CHANNEL_HASH, &d);
    /* Garbage may parse as protobuf by luck, but it must never be reported as
       a valid text message with the original content. */
    if(r == MESH_OK) {
        ASSERT_TRUE(d.data.payload_len != VEC0_TEXT_LEN ||
                    memcmp(d.data.payload, VEC0_TEXT, VEC0_TEXT_LEN) != 0);
    } else {
        ASSERT_TRUE(r == MESH_ERR_BAD_PROTOBUF || r == MESH_ERR_NOT_TEXT);
    }
}

TEST(test_result_names_are_distinct_and_present) {
    ASSERT_TRUE(mesh_decode_result_name(MESH_OK) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_TOO_SHORT) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_CHANNEL_MISMATCH) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_BAD_PROTOBUF) != NULL);
    ASSERT_TRUE(mesh_decode_result_name(MESH_ERR_NOT_TEXT) != NULL);
    ASSERT_TRUE(strcmp(mesh_decode_result_name(MESH_OK),
                       mesh_decode_result_name(MESH_ERR_NOT_TEXT)) != 0);
}

TEST_MAIN_BEGIN()
RUN_TEST(test_acceptance_frame_to_text);
RUN_TEST(test_acceptance_long_frame);
RUN_TEST(test_acceptance_utf8_frame);
RUN_TEST(test_acceptance_second_psk_frame);
RUN_TEST(test_rejects_short_frame);
RUN_TEST(test_rejects_channel_hash_mismatch);
RUN_TEST(test_wrong_key_reports_bad_protobuf_not_ok);
RUN_TEST(test_result_names_are_distinct_and_present);
TEST_MAIN_END()
```

- [ ] **Step 2: Run and verify it fails to compile**

```bash
bash test/host/run_tests.sh
```

Expected: FAIL with `mesh_decode.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/proto/mesh_decode.h`:

```c
#ifndef MESH_DECODE_H
#define MESH_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_channel.h"
#include "mesh_data.h"
#include "mesh_header.h"

/* Failure reasons. These are the buckets the counters view tallies, so each
   one must localize the fault to a single layer. CRC failure is reported by
   the radio and never reaches here. */
typedef enum {
    MESH_OK = 0,
    MESH_ERR_TOO_SHORT,
    MESH_ERR_CHANNEL_MISMATCH,
    MESH_ERR_BAD_PROTOBUF,
    MESH_ERR_NOT_TEXT,
} MeshDecodeResult;

typedef struct {
    MeshHeader header;
    MeshData data;
    uint8_t plaintext[MESH_MAX_PAYLOAD];
    size_t plaintext_len;
} MeshDecoded;

/* Full receive path for one frame: parse header, match channel hash, decrypt,
   decode Data. out->data.payload points into out->plaintext, so MeshDecoded
   must outlive any use of the payload. Nothing is allocated. */
MeshDecodeResult mesh_decode_frame(
    const uint8_t* frame,
    size_t len,
    const uint8_t key[MESH_PSK_LEN],
    uint8_t expected_channel_hash,
    MeshDecoded* out);

/* Stable short label for display and logging. Never NULL. */
const char* mesh_decode_result_name(MeshDecodeResult r);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/proto/mesh_decode.c`:

```c
#include "mesh_decode.h"

#include "mesh_crypto.h"

MeshDecodeResult mesh_decode_frame(
    const uint8_t* frame,
    size_t len,
    const uint8_t key[MESH_PSK_LEN],
    uint8_t expected_channel_hash,
    MeshDecoded* out) {
    if(out == NULL) return MESH_ERR_TOO_SHORT;

    out->plaintext_len = 0;
    out->data.portnum = 0;
    out->data.payload = NULL;
    out->data.payload_len = 0;

    if(!mesh_header_parse(frame, len, &out->header)) return MESH_ERR_TOO_SHORT;

    size_t payload_len = len - MESH_HEADER_LEN;
    if(payload_len > MESH_MAX_PAYLOAD) return MESH_ERR_TOO_SHORT;

    if(out->header.channel != expected_channel_hash) {
        return MESH_ERR_CHANNEL_MISMATCH;
    }

    mesh_crypto_xcrypt(
        key,
        out->header.id,
        out->header.from,
        frame + MESH_HEADER_LEN,
        payload_len,
        out->plaintext);
    out->plaintext_len = payload_len;

    if(!mesh_data_parse(out->plaintext, out->plaintext_len, &out->data)) {
        return MESH_ERR_BAD_PROTOBUF;
    }

    if(out->data.portnum != MESH_PORTNUM_TEXT_MESSAGE_APP) {
        return MESH_ERR_NOT_TEXT;
    }

    return MESH_OK;
}

const char* mesh_decode_result_name(MeshDecodeResult r) {
    switch(r) {
    case MESH_OK: return "ok";
    case MESH_ERR_TOO_SHORT: return "short";
    case MESH_ERR_CHANNEL_MISMATCH: return "chan";
    case MESH_ERR_BAD_PROTOBUF: return "proto";
    case MESH_ERR_NOT_TEXT: return "notxt";
    }
    return "unknown";
}
```

- [ ] **Step 5: Run the full suite**

```bash
bash test/host/run_tests.sh
```

Expected: every test binary reports `PASSED` and the script exits zero.

```bash
echo "exit code: $?"
```

Expected: `exit code: 0`.

- [ ] **Step 6: Record the M0 result**

Append to `docs/measurements.md`:

```markdown
## M0 acceptance, <date>

Synthesized encrypted frames decode to byte-exact original text across all
five vector cases, including a 180 byte payload spanning multiple AES blocks,
a UTF-8 payload, and a non-default PSK index.

Host suite: <N> checks, 0 failures.
```

- [ ] **Step 7: Commit**

```bash
git add src/proto/mesh_decode.h src/proto/mesh_decode.c test/host/test_decode.c docs/measurements.md
git commit -m "Add end-to-end frame decode pipeline"
```

---

## M0 exit criteria

All of the following before M1 begins:

- [ ] `bash test/host/run_tests.sh` exits zero with every binary reporting `PASSED`.
- [ ] A synthesized encrypted frame decodes to byte-exact original text.
- [ ] `docs/measurements.md` records the pinned ufbt SDK version, empty FAP
      size, and free heap measured on the device.
- [ ] `src/proto/` contains no `#include <furi` anywhere. Verify:
      `grep -rn "furi" src/proto/ && echo "VIOLATION" || echo "clean"`
- [ ] No `malloc` or `calloc` in `src/proto/`. Verify:
      `grep -rn "malloc\|calloc" src/proto/ && echo "VIOLATION" || echo "clean"`

## Carried into M1

Unchanged from the spec's open items. None block M0.

1. LongFast bandwidth, spreading factor and coding rate. Derive from the
   `PRESET` macro and `applyModemConfig` in `RadioInterface.cpp`, then cross
   check against the reference node's CLI. Do not assume values.
2. The frequency slot calculation, around `RadioInterface.cpp:1302-1340`.
3. Antenna connector and 915 RF matching on the Electronic Cats board.
4. Per pin and per rail GPIO current limits, needed before M4 transmit.
