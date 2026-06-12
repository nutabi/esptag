#!/usr/bin/env python3
"""Download Apple "Find My" location reports for the tag's epoch-0 identity.

This is the report-fetching counterpart to scan_findmy.py. Where the scanner
confirms the tag's advertisement is heard *locally* over BLE, this script asks
Apple's servers for the crowd-sourced location reports that nearby iPhones have
uploaded for the tag's advertising key -- i.e. where the tag has actually been
seen.

To decrypt those reports you need the *private* scalar behind the advertised
key, not just the public `p_curr` the scanner sees: each report is ECIES-
encrypted to the advertising public key, so only the holder of the matching
private key can recover the latitude/longitude. We reconstruct that private key
straight from the provisioned seed (seed.csv):

    d_0, sk_0           <- seed.csv (the two root secrets)
    (u, v) = KDF(sk_0, "diversify", 72)
    d_epoch = (d_0*u + v) mod n           <- epoch-0 private scalar (28-byte BE)
    p_0     = x(d_epoch * G)              <- the advertised key (matches firmware)

This mirrors crypto_derive_p() in main/crypto.c at counter 0 (sk_curr == sk_0).

STATIC KEY ASSUMPTION: this computes the epoch-0 identity *once* and queries only
that key. It is the right tool for a tag built with CONFIG_ESPTAG_ROTATE_ENABLE=n
(static identity), or to look up the very first epoch of a rotating tag. A
rotating tag changes identity every CONFIG_ESPTAG_ROTATE_INTERVAL_MS, so reports
for later epochs live under p_1, p_2, ... -- which this script does not derive.
(Iterating sk via crypto_update_sk and re-deriving for each epoch, then querying
the whole key set, would be the rotating-tag extension; not done here.)

Reports are fetched for the last 7 days (Apple's retention window).

Apple account: fetching reports needs an authenticated Apple account plus an
anisette provider (Apple's anti-abuse device attestation). By default this runs
anisette *locally* in-process via the `anisette` library (no Docker/server
needed -- it downloads Apple's libraries on first use); pass --anisette-server to
use a remote anisette server instead. On first run, log in with --email/--password
(you'll be prompted for a 2FA code) and the session is saved to --account-data;
later runs restore from that file and skip login.

Run with the venv in this directory (where findmy is installed):

    scripts/.venv/bin/python scripts/fetch_reports.py --email you@icloud.com
    scripts/.venv/bin/python scripts/fetch_reports.py        # reuse saved session
    scripts/.venv/bin/python scripts/fetch_reports.py --key-only   # just print p_0

WARNING: seed.csv holds the tag's root secret; this script reads it and derives
the tag's private key in memory. Treat both the seed and the saved account
session (--account-data) as sensitive.
"""

from __future__ import annotations

import argparse
import getpass
import hashlib
import sys
from pathlib import Path

try:
    from findmy import KeyPair
    from findmy.reports import (
        AppleAccount,
        LocalAnisetteProvider,
        LoginState,
        RemoteAnisetteProvider,
    )
except ImportError:
    sys.exit("findmy is not installed. Use scripts/.venv/bin/python, "
             "or: pip install findmy")

# Size constants -- mirror crypto.h (D_LEN, SK_LEN) and gen_seed.py.
D_LEN = 28
SK_LEN = 32

# secp224r1 group order n, big-endian. Mirrors P224_N in crypto.c.
P224_N = int.from_bytes(
    bytes.fromhex("FFFFFFFFFFFFFFFFFFFFFFFFFFFF16A2E0B8F03E13DD29455C5C2A3D"),
    "big",
)

# NVS namespace / keys provisioned by gen_seed.py -- must match nvs_store.c.
NAMESPACE = "esptag"
KEY_D0 = "d_0"
KEY_SK0 = "sk_0"


def kdf(z: bytes, info: bytes, out_len: int) -> bytes:
    """SP-800-108-style SHA-256 counter-mode KDF. Mirrors gen_kat.py / crypto.c."""
    out = b""
    counter = 1
    while len(out) < out_len:
        out += hashlib.sha256(z + counter.to_bytes(4, "big") + info).digest()
        counter += 1
    return out[:out_len]


def load_seed(path: Path) -> tuple[bytes, bytes]:
    """Parse seed.csv, returning (d_0, sk_0) as raw big-endian bytes.

    The CSV is the hex2bin NVS image gen_seed.py emits: a header row, the
    namespace row, then one `key,data,hex2bin,<hex>` row per secret.
    """
    values: dict[str, bytes] = {}
    for raw in path.read_text().splitlines():
        parts = raw.split(",")
        if len(parts) == 4 and parts[2] == "hex2bin":
            values[parts[0]] = bytes.fromhex(parts[3])

    try:
        d_0, sk_0 = values[KEY_D0], values[KEY_SK0]
    except KeyError as e:
        sys.exit(f"{path}: missing {e.args[0]!r} row (not a valid esptag seed?)")
    if len(d_0) != D_LEN or len(sk_0) != SK_LEN:
        sys.exit(f"{path}: unexpected key sizes "
                 f"(d_0={len(d_0)}B, sk_0={len(sk_0)}B; want {D_LEN}/{SK_LEN})")
    return d_0, sk_0


def derive_epoch0_private(d_0: bytes, sk_0: bytes) -> bytes:
    """Return the epoch-0 private scalar d_epoch = (d_0*u + v) mod n (28-byte BE).

    counter == 0, so sk_curr == sk_0; this is crypto_derive_p's scalar step.
    """
    uv = kdf(sk_0, b"diversify", 72)
    u = int.from_bytes(uv[:36], "big")
    v = int.from_bytes(uv[36:], "big")
    d_epoch = (int.from_bytes(d_0, "big") * u + v) % P224_N
    return d_epoch.to_bytes(D_LEN, "big")


def get_account(args) -> AppleAccount:
    """Restore a saved Apple account session, or log in interactively and save it."""
    account_path = Path(args.account_data)

    if account_path.exists():
        acc = AppleAccount.from_json(account_path)
        if acc.login_state == LoginState.LOGGED_IN:
            print(f"Restored session for {acc.account_name} from {account_path}.")
            return acc
        print(f"Saved session in {account_path} is not logged in; re-authenticating.")
    else:
        if args.anisette_server:
            anisette = RemoteAnisetteProvider(args.anisette_server)
        else:
            # Run anisette in-process; cache the downloaded Apple libs next to
            # the saved session so future first-logins don't re-download them.
            anisette = LocalAnisetteProvider(
                libs_path=account_path.with_name("anisette-libs.bin"))
        acc = AppleAccount(anisette)

    email = args.email or input("Apple ID email: ")
    password = args.password or getpass.getpass("Apple ID password: ")
    state = acc.login(email, password)

    if state == LoginState.REQUIRE_2FA:
        methods = acc.get_2fa_methods()
        for i, m in enumerate(methods):
            label = getattr(m, "phone_number", None) or type(m).__name__
            print(f"  [{i}] {label}")
        choice = methods[int(input("2FA method index: "))]
        choice.request()
        choice.submit(input("2FA code: "))

    acc.to_json(account_path)
    print(f"Logged in as {acc.account_name}; session saved to {account_path}.")
    return acc


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--seed", default=Path(__file__).resolve().parent.parent / "seed.csv",
                        type=Path,
                        help="path to the provisioned seed CSV (default: ../seed.csv next to the repo root)")
    parser.add_argument("--key-only", action="store_true",
                        help="just derive and print p_0 (the advertised key); no network")
    parser.add_argument("--email", default=None,
                        help="Apple ID email (prompted if omitted on first login)")
    parser.add_argument("--password", default=None,
                        help="Apple ID password (prompted if omitted; avoid on shared hosts)")
    parser.add_argument("--anisette-server", default=None, metavar="URL",
                        help="use a remote anisette server for first login "
                             "(e.g. http://localhost:6969); default runs anisette locally")
    parser.add_argument("--account-data", default="account.json",
                        help="path to save/restore the Apple session (default: account.json)")
    args = parser.parse_args()

    d_0, sk_0 = load_seed(args.seed)
    private = derive_epoch0_private(d_0, sk_0)
    keypair = KeyPair(private)
    p_0 = keypair.adv_key_bytes  # 28-byte advertised key == firmware's p_0

    print(f"epoch-0 advertised key p_0 = {p_0.hex()}")
    print(f"epoch-0 BLE address        = {keypair.mac_address}")
    if args.key_only:
        return

    acc = get_account(args)
    try:
        reports = acc.fetch_location_history(keypair)
    finally:
        # AppleAccount.close() is a coroutine even on the sync wrapper; drive it
        # on the account's own event loop rather than leaving it unawaited.
        acc._evt_loop.run_until_complete(acc.close())

    if not reports:
        print("\nNo location reports in the last 7 days.")
        return

    print(f"\n{len(reports)} report(s) (oldest first):")
    for r in sorted(reports):
        print(f"  {r.timestamp.isoformat()}  "
              f"lat={r.latitude:+.6f} lon={r.longitude:+.6f}  "
              f"±{r.horizontal_accuracy}m  conf={r.confidence}")


if __name__ == "__main__":
    main()
