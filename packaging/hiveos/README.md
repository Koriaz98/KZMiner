# KZMiner on HiveOS

This directory contains everything needed to run KZMiner as a
HiveOS Custom Miner, plus an importable flight sheet template.

## Quick setup (recommended)

1. Download `kzminer-flightsheet-btc09.json` from the
   [latest release](https://github.com/Koriaz98/KZMiner/releases/latest).
2. In HiveOS: **Flight Sheets → Add New Flight Sheet**.
3. Select coin **09C** (or add it if you haven't already).
4. Click **Import from → File**, and select the downloaded JSON file.
5. Select your wallet (this isn't included in the template, since
   it's specific to you).
6. Adjust `--intensity` in "Extra config arguments" to suit your
   hardware, if needed.
7. Click **Create Flight Sheet**, then apply it to your rig(s).

## Manual setup (alternative)

If you'd rather fill in the "Custom configuration" form yourself:

| Field | Value |
|---|---|
| Miner name | `KZMiner-X-Y-Z` (matching the release version, e.g. `KZMiner-0-9-10`) |
| Installation URL | `https://github.com/Koriaz98/KZMiner/releases/download/vX.Y.Z/KZMiner-X-Y-Z-HiveOS.tar.gz` |
| Wallet and worker template | `%WAL%.%WORKER_NAME%` |
| Pool URL | `https://btc09.org` |
| Extra config arguments | `--mode solo -o %URL% -u %WAL%.%WORKER_NAME% --cpu --gpu --intensity 5` |

## Updating to a new version

Simply edit your existing flight sheet's **Miner name** and
**Installation URL** fields to reference the new version, then
re-apply it. HiveOS downloads and installs the new version
automatically — no manual cleanup needed.

**The only exception**: if you ever re-download the *same* filename
after its contents changed on GitHub (for example, if you're testing
a pre-release build we've asked you to try, and we've updated it
under the same name), HiveOS may keep serving a cached copy from
`/hive/miners/custom/downloads/`. This does not happen with normal
version updates, since each release uses a distinct filename. If you
ever do run into it, this clears it:

```bash
rm -rf /hive/miners/custom/KZMiner-X-Y-Z
rm -f /hive/miners/custom/downloads/KZMiner-X-Y-Z*
pkill -f "kzminer"
```

(replace `X-Y-Z` with the version in question), then re-apply your
flight sheet.

## Viewing live output

Run `miner` from the rig's SSH/console session to attach to KZMiner's
live dashboard, exactly as you would for any other HiveOS miner.

## Files in this directory

- `kzminer/h-manifest.conf`, `h-config.sh`, `h-run.sh`, `h-stats.sh` —
  the HiveOS Custom Miner integration scripts (bundled into each
  release's `KZMiner-X-Y-Z-HiveOS.tar.gz` alongside the binary).
- `kzminer-flightsheet-btc09.json` — importable flight sheet template
  for BTC09 (Argon2id-64MiB), KZMiner's only currently supported coin.
