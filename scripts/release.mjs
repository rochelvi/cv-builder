#!/usr/bin/env node
// npm run release
//
// Builds the release artifacts for this machine's platform and puts them,
// with SHA256SUMS.txt, in releases/<version>/:
//
//   Windows  CV Builder_<version>_x64-setup.exe (NSIS) and cvcli.exe
//   Linux    .deb, .AppImage and cvcli
//
// The build is made reproducible in what it reveals: every local path - the
// checkout, CARGO_HOME, RUSTUP_HOME, the home directory - is remapped with
// --remap-path-prefix, and the finished executables are then searched for the
// build directory and the user name. A hit fails the release: an executable
// that names the machine it was built on is not shipped.

import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { homedir, userInfo } from "node:os";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const isWindows = process.platform === "win32";
const exe = isWindows ? ".exe" : "";

function fail(message) {
  console.error(`release: ${message}`);
  process.exit(1);
}

function run(command, args, env = {}) {
  console.log(`> ${command} ${args.join(" ")}`);
  const result = spawnSync(command, args, {
    cwd: root,
    stdio: "inherit",
    env: { ...process.env, ...env },
  });
  if (result.status !== 0) fail(`${command} exited with ${result.status ?? result.signal}`);
}

function capture(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: "utf8" });
  if (result.status !== 0) fail(`${command} ${args.join(" ")} failed`);
  return result.stdout.trim();
}

// ------------------------------------------------------------ what to build

const hostTriple = capture("rustc", ["-vV"])
  .split("\n")
  .find((line) => line.startsWith("host:"))
  ?.slice(6)
  .trim();
if (!hostTriple) fail("cannot tell the host target triple");

const version = JSON.parse(readFileSync(join(root, "package.json"), "utf8")).version;
const cargoVersion = /^\s*version\s*=\s*"([^"]+)"/m.exec(readFileSync(join(root, "Cargo.toml"), "utf8"))?.[1];
if (version !== cargoVersion) fail(`package.json says ${version}, Cargo.toml says ${cargoVersion}`);

// ------------------------------------------------------------- the rustflags

// RUSTFLAGS in the environment replaces, not extends, what .cargo/config.toml
// sets for the target - so the hardening flags from there are read and kept.
function configFlags(triple) {
  const config = join(root, ".cargo", "config.toml");
  if (!existsSync(config)) return [];
  const text = readFileSync(config, "utf8");
  const section = text.split(/^\[/m).find((part) => part.startsWith(`target.${triple}]`));
  const list = section && /rustflags\s*=\s*\[([^\]]*)\]/.exec(section)?.[1];
  return list ? [...list.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1].replace(/\\(.)/g, "$1")) : [];
}

const cargoHome = process.env.CARGO_HOME ?? join(homedir(), ".cargo");
const rustupHome = process.env.RUSTUP_HOME ?? join(homedir(), ".rustup");
// The last matching remap wins, so the most specific prefixes come last.
const remaps = [
  [homedir(), "/home"],
  [rustupHome, "/rustup"],
  [cargoHome, "/cargo"],
  [root, "/cv-builder"],
];
const flags = [...configFlags(hostTriple), ...remaps.map(([from, to]) => `--remap-path-prefix=${from}=${to}`)];
// The unit separator keeps paths with spaces in one piece.
const rustEnv = { CARGO_ENCODED_RUSTFLAGS: flags.join("\x1f") };

// ------------------------------------------------------------------- build

const out = join(root, "releases", version);
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

run("cargo", ["test", "--workspace", "--release"], rustEnv);
// The CLIs are run with this very node rather than through npx, which on
// Windows would need a shell to start a .cmd.
const tool = (path) => join(root, "node_modules", ...path.split("/"));
run(process.execPath, [tool("typescript/bin/tsc"), "--noEmit"]);

run("cargo", ["build", "--release", "-p", "cvcli"], rustEnv);
const cvcli = join(root, "target", "release", `cvcli${exe}`);
const binaries = join(root, "src-tauri", "binaries");
mkdirSync(binaries, { recursive: true });
copyFileSync(cvcli, join(binaries, `cvcli-${hostTriple}${exe}`));

run(process.execPath, [tool("@tauri-apps/cli/tauri.js"), "build", "--config", "src-tauri/tauri.release.conf.json"], rustEnv);

// ------------------------------------------------- nothing local in the binaries

const app = join(root, "target", "release", `cv-builder${exe}`);
const needles = new Set([root, cargoHome, rustupHome, homedir(), userInfo().username].filter((s) => s && s.length >= 3));

function variants(text) {
  // As written, with the other slash, in any case; UTF-8 and UTF-16LE.
  const forms = new Set([text, text.replaceAll("\\", "/"), text.replaceAll("/", "\\")]);
  const out = [];
  for (const form of forms) {
    out.push(Buffer.from(form.toLowerCase(), "utf8"), Buffer.from(form.toLowerCase(), "utf16le"));
  }
  return out;
}

function findLocalPaths(file) {
  const lower = Buffer.from(readFileSync(file).toString("latin1").toLowerCase(), "latin1");
  const hits = [];
  for (const needle of needles) {
    for (const bytes of variants(needle)) {
      const at = lower.indexOf(bytes);
      if (at >= 0) {
        hits.push(`${JSON.stringify(needle)} at byte ${at}`);
        break;
      }
    }
  }
  return hits;
}

let leaks = 0;
for (const file of [app, cvcli]) {
  const hits = findLocalPaths(file);
  if (hits.length) {
    leaks += hits.length;
    console.error(`release: ${basename(file)} contains ${hits.join(", ")}`);
  } else {
    console.log(`checked: ${basename(file)} names no local path or user`);
  }
}
if (leaks) fail("build paths found in the executables; not releasing");

// ---------------------------------------------------------------- collect

const bundle = join(root, "target", "release", "bundle");
const artifacts = [];
for (const kind of ["nsis", "deb", "appimage"]) {
  const dir = join(bundle, kind);
  if (!existsSync(dir)) continue;
  for (const name of readdirSync(dir)) {
    const file = join(dir, name);
    if (statSync(file).isFile() && /\.(exe|deb|AppImage)$/.test(name) && name.includes(version)) {
      copyFileSync(file, join(out, name));
      artifacts.push(name);
    }
  }
}
const cliName = isWindows ? `cvcli-${version}-windows-x86_64.exe` : `cvcli-${version}-linux-x86_64`;
copyFileSync(cvcli, join(out, cliName));
artifacts.push(cliName);
if (artifacts.length < 2) fail("no installer or package was produced");

const sums = artifacts
  .sort()
  .map((name) => `${createHash("sha256").update(readFileSync(join(out, name))).digest("hex")}  ${name}`)
  .join("\n");
writeFileSync(join(out, "SHA256SUMS.txt"), `${sums}\n`);

console.log(`\nreleases/${version}/`);
for (const name of [...artifacts, "SHA256SUMS.txt"]) console.log(`  ${name}`);
