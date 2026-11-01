# Third-party files used by the page

None of these are committed. `fetch-v86.sh` downloads them into `site/v86/`
at build time and checks each against a SHA-256 recorded in the script.

| file | source | pin | licence |
|---|---|---|---|
| `libv86.js`, `v86.wasm` | npm package [`v86`](https://www.npmjs.com/package/v86), `build/` | version 0.5.471 (published 2026-10-09) | BSD-2-Clause, copied to `v86/LICENSE.v86` |
| `seabios.bin`, `vgabios.bin` | [copy/v86](https://github.com/copy/v86) `bios/` | commit `94faf3d6b19ecabc53a4010aaa24252fa6a162e2` (the commit npm 0.5.471 was built from) | SeaBIOS and its VGA BIOS: LGPL-3.0, copied to `v86/COPYING.LESSER` |

To move the pin, change `V86_NPM_VERSION`, `V86_COMMIT` and the checksums in
`fetch-v86.sh` together, and the table above.

The page itself (`index.html`, `app.js`, `style.css`) uses no other
libraries.
