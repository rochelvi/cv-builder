# Reference output of the C++ implementation

Captured from the last C++ build (MSVC, `resume_core` at commit 2a8a02b) before
the port to Rust, so the port can be checked against what the program actually
produced rather than against a reading of the old code.

| File | What it holds |
| --- | --- |
| `input/*.json` | Resumes fed to the old program: the sample, the CV pinned by the old `test_pdf.cpp`, a legacy file without `sections`, one with every wrong type the reader tolerates, a five-page one, an empty object and one with a BOM |
| `golden/<name>.saved.json` | `toJson(fromJson(input))` — what Save writes for that file |
| `golden/<name>.layout.json` | The laid-out document, every double in shortest round-trip form |
| `golden/<name>.pdf` | Exported PDF bytes; `summary.txt` lists pages, size and FNV-1a 64 |
| `golden/<name>.ops.json` | `cvcli --dump-ops` for the same input |
| `golden/default.saved.json`, `new.saved.json` | A default-constructed CV and the "New" CV, saved |
| `golden/numfmt.tsv` | value, `fixed(v,3)`, `fixed(v,0)`, `shortest(v)` |
| `golden/font.txt` | Both faces: header metrics, the BMP cmap, every advance, FNV of three subsets |
| `golden/cli.*.txt` | `cvcli --version`, `--font-report` and the usage text |

`reference.pdf` is the file whose hash the old test pinned (`7410097b186e6353`,
80832 bytes). These files are the specification: if the Rust output differs,
the Rust output is wrong unless a commit says otherwise.
