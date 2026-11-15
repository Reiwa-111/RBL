# RBL Studio app-local CPython runtime

- CPython version: `3.14.10`
- Platform: `windows-x86_64`
- Upstream archive: `python-3.14.10-embed-amd64.zip`
- Upstream URL: https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip
- Archive size: `12619664` bytes
- SHA-256: `90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159`
- Interpreter path: `python.exe`
- The runtime is invoked app-locally; no system Python or inherited Python PATH is required.
- No additional Python packages are required by the RBL backend; the backend uses the standard library.
- The runtime is a pruned copy of the official embeddable distribution. Optional extension modules not used by the RBL backend (TLS/SQLite/zstd/decimal/XML and similar) were removed to reduce payload size; the remaining modules are the ones exercised by the 63-case regression suite and the IDE backend.
- Corresponding-source archives for CPython and python-build-standalone are **not** bundled in this checkout. Upstream URLs, hashes and source references are recorded in `rbl-python-runtime.json`; the public source-delivery route still needs owner/legal approval.
- The upstream embeddable ZIP `LICENSE.txt` and the full CPython/third-party license inventory under `licenses/` are retained; `python314._pth` remains isolated with `site` disabled.

This inventory records provenance and hashes; it is not legal approval for redistribution.
