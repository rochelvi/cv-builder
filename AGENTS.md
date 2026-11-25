# Working on this repository

## Build and test

Tauri 2: Rust in `crates/` and `src-tauri/`, TypeScript + Vite in `src/`.

```bash
npm ci
cargo test --workspace      # every Rust test, fixtures included
npx tsc --noEmit            # the page
npm run tauri dev           # the window, rebuilt on change
npm run release             # installer, cvcli, SHA256SUMS.txt in releases/<version>/
```

A step is done when `cargo test --workspace` and `npx tsc --noEmit` pass and
`npm run tauri build -- --no-bundle` builds. `cargo clippy --workspace
--all-targets` is kept clean.

The crates follow the layers, and that is load-bearing: `cvb-core` has no
Tauri, no file system and no operating system in it - bytes in, bytes out -
which is what lets `cvcli` link it alone and stay a single small executable.

## Things that will bite

* **`fixtures/` is the specification.** It holds what the last C++ build wrote
  for seven resumes: saved JSON, the layout to the last bit, PDF bytes, cvcli
  output. If a change breaks a fixture test, the change is wrong - unless it is
  deliberate, and then the fixture moves in the same commit, and that commit
  says every exported resume now looks different. The pinned PDF hash
  (`7410097b186e6353`) is in `crates/cvb-core/src/pdf.rs`.
* **Floating-point order in `layout.rs` matters.** Sums are taken in the C++
  order; `a + b + c` and `a + (b + c)` can differ in the last bit, and that bit
  can push a block onto the next page.
* **Do not change the bundled font without measuring.** Advance widths decide
  line breaks. `font.rs` pins every metric; `cvcli --dump-ops` and
  `--font-report` compare faces (see `assets/fonts/README.md`).
* **The JSON reader is our own on purpose.** Key order, first duplicate wins,
  lax numbers, byte offsets in errors: saving an opened file must give back the
  same bytes. Do not swap it for serde_json in the model.
* **Paths never come from the page.** Dialogs run in Rust, recent files are
  addressed by index, drops and second launches are queued in Rust. Every
  outside path goes through `files::check_path`; every write through
  `files::write_atomic`. New security checks get their own test.
* **A new command** goes into `generate_handler!` in `lib.rs`, into `COMMANDS`
  in `src-tauri/build.rs`, as `allow-<name>` into
  `src-tauri/capabilities/main-window.json`, and into `src/api.ts`.
* **Commands are async.** A native dialog blocks its thread; sync commands run
  on the main thread, whose event loop must keep turning. Never hold the
  workspace lock while a dialog is open.
* **No innerHTML.** DOM is built with `el()` / `svg()` from `src/dom.ts`; the CSP
  forbids inline script and style, so set styles through `element.style`.
* **cvcli is a sidecar only in release builds** (`tauri.release.conf.json`):
  tauri-build checks `externalBin` at compile time, so the base config must not
  name it or `cargo test` would need cvcli built first.
* **Heredocs in the Bash tool** may collapse `\\` and turn `\uXXXX` into
  characters. Write files with escapes through an editor, not a shell.

## This machine, specifically

Windows has Rust (MSVC), Visual Studio Build Tools 2022 and Node: everything
builds natively, `npm run release` included. WSL Ubuntu has no C compiler and
`sudo` asks for a password, so neither the old C++ nor a Linux Tauri build runs
there until someone installs `build-essential` and `libwebkit2gtk-4.1-dev`.

To look at the running window without a display tool: start it with
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9333` and talk
to `http://127.0.0.1:9333/json` over the Chrome DevTools Protocol
(`Page.captureScreenshot`, `Runtime.evaluate`).
