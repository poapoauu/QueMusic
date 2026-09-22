# Navidrome smoke test

`quemusic_navidrome_smoke` is an opt-in connection check. It is built with the
project but is deliberately not registered with CTest.

Set the connection values in your own shell; do not put them in a command
history shared with others, source files, test logs, or this repository:

```bash
export QUEMUSIC_NAVIDROME_URL
export QUEMUSIC_NAVIDROME_USER
export QUEMUSIC_NAVIDROME_PASSWORD
/private/var/folders/.../quemusic-navidrome-build/bin/quemusic_navidrome_smoke
```

The executable requires all three variables. It performs the v2 open/ping and
capability negotiation, loads a recommendation page, resolves a stream for a
returned test track, then creates, renames, and deletes a temporary playlist.
The smoke account therefore needs playlist write permission.

Favorite mutation is disabled by default because it changes a real library.
Enable it only for a track explicitly designated for destructive smoke tests:

```bash
export QUEMUSIC_NAVIDROME_ENABLE_FAVORITE_ROUNDTRIP=1
export QUEMUSIC_NAVIDROME_FAVORITE_TRACK_ID='provider-native-test-track-id'
```

When enabled, the executable reads the designated track's current favorite state,
toggles it, and restores that original state. On failure or timeout it makes one
best-effort cleanup pass with a separate short timeout: any known temporary
playlist is deleted and any attempted favorite mutation is restored. Cleanup
failures are reported without credentials and are not retried recursively.
After an interrupted process or failed cleanup, inspect and remove any playlist
named `QueMusic smoke ...` and verify the designated track's favorite state.

Output contains only operation, outcome, numeric error kind, and elapsed time.
It never prints configuration values, requests, headers, authenticated URLs,
tokens, salts, passwords, secret references, or response bodies.

An exit code of `0` means the complete sequence succeeded. `64` means required
environment is missing, including the designated track when favorite mutation
is enabled. Other nonzero results identify the failed v2 stage without exposing
server data. This command is intentionally not part of CTest.
