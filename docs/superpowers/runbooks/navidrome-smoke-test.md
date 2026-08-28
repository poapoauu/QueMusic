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

The executable requires all three variables. It performs `ping`, a small
search, root browse, stream resolution, artwork resolution, and lyric lookup
in sequence when the search returns a playable track. Its output contains only
the operation, outcome, error kind, and elapsed time; it does not print
configuration values, requests, headers, tokens, salts, or response bodies.

An exit code of `0` means the complete sequence succeeded. `64` means required
environment is missing. A nonzero result after a successful search can mean the
library has no playable matching track, artwork, or lyrics; the printed error
kind identifies the protocol result without exposing server data.
