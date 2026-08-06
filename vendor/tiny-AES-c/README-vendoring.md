# tiny-AES-c

Vendored from https://github.com/kokke/tiny-AES-c

- Commit: `23856752fbd139da0b8ca6e471a13d5bcc99a08d`
- License: Unlicense, public domain. See `unlicense.txt`.
- Files: `aes.c` and `aes.h`, copied unmodified.

## Why it is here

Meshtastic encrypts channel traffic with AES128 in CTR mode. The Flipper's own
AES hardware acceleration cannot be used for it: every raw key path in
`furi_hal_crypto` hardcodes `CRYPTO_KEYSIZE_256B` (`furi_hal_crypto.c:203` and
`:400`), so it only does AES256. The STM32WB's AES block supports AES128, but
the HAL never exposes it, and the only AES128-aware path is the secure enclave,
which is append only and off limits to public applications.

## Configuration

`aes.c` and `aes.h` are unmodified. The mode selection uses `#ifndef` guards, so
the build passes the configuration in as compiler flags instead of editing the
files:

    -DCBC=0 -DECB=0 -DCTR=1

`AES128` is already the library default. Keeping the sources byte-identical to
upstream means updating is a plain copy with no merge.

## Counter semantics

`AES_CTR_xcrypt_buffer` increments the whole 16 byte counter block as a big
endian integer. Meshtastic calls `setCounterSize(4)`, so only the low 4 bytes
increment.

These agree for any payload short enough that the low 4 bytes never overflow. A
Meshtastic frame is at most 255 bytes, or 16 blocks, and the counter starts at
zero, so it never comes close. Do not "fix" this difference.
