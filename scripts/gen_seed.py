#!/usr/bin/env python3
"""Generate an NVS partition CSV holding a fresh tag seed.

The CSV is consumed by ESP-IDF's NVS partition generator
(`nvs_create_partition_image` / `nvs_partition_gen.py`). It provisions the
two root secrets the firmware needs:

  - d_0  : 28-byte initial private scalar, a valid secp224r1 scalar in [1, n-1]
  - sk_0 : 32-byte initial symmetric key (unconstrained random bytes)

The SK ratchet counter is not persisted; firmware initialises it to 0 on boot.

Values are emitted inline as hex (encoding `hex2bin`), so the CSV is
self-contained -- no companion binary files to track.

By default secrets come from the OS CSPRNG (`secrets`/`os.urandom`). Passing
--seed makes generation deterministic via a seeded PRNG; this is reproducible
but NOT cryptographically secure, and is intended only for testing.

WARNING: the emitted CSV contains the tag's root secret in plaintext. Treat the
file as sensitive and pair on-device storage with flash/NVS encryption for any
real deployment.

DEPLOYMENT NOTE (at-rest / at-runtime secret protection): the root secret
(d_0, sk_0) lives in the `nvs` partition in plaintext and is also resident in
RAM for the device's lifetime (d_0 is needed on every rotation). On a captured
tag it is therefore recoverable by a flash dump (esptool read_flash) or by
halting the CPU over USB-JTAG. This is NOT fixed here: the target ESP32-S3R8
module has no secure element, and the security review accepted this as a known
limitation. A hardened build would need Flash Encryption (release) + Secure Boot
v2 + NVS encryption for this namespace + USB-JTAG disabled via eFuse -- which
binds the secret to one chip but, absent a secure element, still cannot stop a
determined invasive (decap/probe) attack. See the security pass notes.
"""

import argparse
import os
import secrets
import sys

# Size constants -- mirror crypto.h (D_LEN, SK_LEN).
D_LEN = 28
SK_LEN = 32

# secp224r1 group order n, big-endian. Mirrors P224_N in crypto.c.
P224_N = int.from_bytes(
    bytes.fromhex("FFFFFFFFFFFFFFFFFFFFFFFFFFFF16A2E0B8F03E13DD29455C5C2A3D"),
    "big",
)

# NVS namespace / keys -- must match nvs_store.c.
NAMESPACE = "esptag"
KEY_D0 = "d_0"
KEY_SK0 = "sk_0"


def make_rng(seed):
    """Return a callable rng(n) -> n random bytes.

    With no seed, draw from the OS CSPRNG. With a seed, derive bytes
    deterministically from a seeded PRNG (reproducible, not secure).
    """
    if seed is None:
        return secrets.token_bytes

    import random

    prng = random.Random(seed)
    return lambda n: bytes(prng.getrandbits(8) for _ in range(n))


def gen_d0(rng):
    """Return a 28-byte big-endian secp224r1 scalar in [1, n-1].

    Rejection sampling over D_LEN-byte draws keeps the result uniform and
    avoids the modulo bias of reduce-mod-n.
    """
    while True:
        candidate = int.from_bytes(rng(D_LEN), "big")
        if 1 <= candidate < P224_N:
            return candidate.to_bytes(D_LEN, "big")


def build_csv(d_0, sk_0):
    lines = [
        "key,type,encoding,value",
        f"{NAMESPACE},namespace,,",
        f"{KEY_D0},data,hex2bin,{d_0.hex()}",
        f"{KEY_SK0},data,hex2bin,{sk_0.hex()}",
    ]
    return "\n".join(lines) + "\n"


def parse_seed(value):
    """Accept an int (decimal or 0x hex) or fall back to the raw string."""
    try:
        return int(value, 0)
    except ValueError:
        return value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "-o", "--output", default="seed.csv",
        help="output CSV path (default: seed.csv; use - for stdout)",
    )
    parser.add_argument(
        "-s", "--seed", type=parse_seed, default=None,
        help="optional CSPRNG seed for reproducible, INSECURE output (testing only)",
    )
    args = parser.parse_args(argv)

    rng = make_rng(args.seed)
    d_0 = gen_d0(rng)
    sk_0 = rng(SK_LEN)

    csv = build_csv(d_0, sk_0)

    if args.output == "-":
        sys.stdout.write(csv)
    else:
        # Restrictive permissions: this file holds the root secret.
        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(csv)
        print(f"wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
