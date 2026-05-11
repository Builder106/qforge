# Contributing to qforge

Thanks for considering a contribution. qforge is a zero-dependency C99 deep-learning framework with a WASM-powered web demo on top. The codebase is small (~2,000 lines of C), test-driven, and intentionally readable — every piece is meant to be inspectable from the bottom up.

## TL;DR

```bash
git clone https://github.com/Builder106/qforge.git
cd qforge
make test           # 56 unit tests   (~2s)
make integration    # 40 black-box tests on compiled binaries (~30s)
```

If both pass, your environment is ready.

## Prerequisites

| Tool | Required for | Notes |
|---|---|---|
| `gcc` or `clang` (C99) | core engine, examples, unit + integration tests | macOS ships with clang; Ubuntu: `apt install build-essential` |
| `make` | every build target | macOS: pre-installed; Linux: usually pre-installed |
| `emcc` ([Emscripten](https://emscripten.org)) | WASM build | only needed if you touch the in-browser demo |
| `node` 20+ | end-to-end tests | only for `make e2e` |
| `python3` | local web server | only for previewing `web/` locally |

You do **not** need a Python venv, npm globally, or any framework — the C engine and integration tests have zero external dependencies beyond a libc and libm.

## Repository layout

```
qforge/
├── include/                # public C headers (one per module)
├── src/                    # C implementations
├── tests/                  # unit tests (TDD, custom zero-dep harness)
│   ├── test_harness.h
│   ├── test_*.c            # per-module suites
│   ├── test_scenarios.c    # multi-module integration tests
│   └── integration/run.sh  # black-box tests of compiled binaries
├── examples/               # demo programs (xor, dqn_trader, market_generator, …)
├── web/                    # static site deployed to Vercel
│   ├── index.html / app.js / styles.css
│   ├── wasm/               # Emscripten output (committed; rebuilt by `make wasm`)
│   └── casts/              # asciinema recordings
├── e2e/                    # Playwright + playwright-bdd suite
│   ├── features/*.feature  # Gherkin scenarios
│   └── steps/*.ts          # step definitions
├── .github/workflows/      # CI (tests + WASM drift check)
├── docs/PRD.md             # original product brief
├── README.md
├── CONTRIBUTING.md         # ← you are here
├── LICENSE
└── Makefile
```

## Build targets

| Command | What it does |
|---|---|
| `make test` | Build and run all C unit tests (56). Required to pass on every commit. |
| `make integration` | Build the example binaries and run black-box assertions on their stdout (40 checks). |
| `make memcheck` | Run unit tests under AddressSanitizer (or Valgrind if installed). |
| `make wasm` | Compile every example to a WebAssembly module. Requires `emcc`. |
| `make e2e-install` | Install Playwright + Chromium for end-to-end tests (one-time, ~300MB). |
| `make e2e` | Run the Playwright BDD suite against a local server. |
| `make dqn` / `make market_gen` / `make examples` / `make bench` / `make gradcheck` | Build + run each demo program. |
| `make clean` | Remove all build artifacts (object files, binaries, `.dSYM/`). |
| `make wasm-clean` | Remove the WASM build output. |

## Code style

The project follows a few non-negotiable conventions:

1. **C99, strict warnings.** All translation units must compile cleanly with `-std=c99 -Wall -Wextra -Werror -pedantic`. CI fails on warnings.
2. **Test-first.** New engine functionality belongs in `tests/test_*.c` *before* the implementation. The commit history walks through this — see `feat: implement tensor module with tests (TDD)`.
3. **Heap-allocated tensors are caller-owned.** Every tensor operation in `src/tensor.c` returns a fresh `Tensor*`. Callers must `tensor_free()` the result. Don't introduce in-place mutation without a strong reason.
4. **Fail fast on shape mismatches.** `assert()` for dimension errors. The library is internal — callers should not pass invalid shapes, so asserts are correct rather than defensive `if`-then-`return`.
5. **No external dependencies.** Adding `-l<something>` to the link line is a no. The only allowed include outside `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`, `<assert.h>`, `<time.h>` is the project's own headers.
6. **Comments explain *why*, not *what*.** Identifiers should carry the *what*. Comments are for non-obvious invariants, hidden constraints, or workarounds for specific bugs.

The frontend (`web/`) follows the same spirit — vanilla JS, no bundler, no framework. The page works without JavaScript-the-platform churn.

## Workflows for common changes

### Adding a function to the C engine

1. **Write the failing test first.** Add `void test_my_thing(void)` in the appropriate `tests/test_*.c` and wire it into `tests/test_main.c` (both the `extern` declaration and the `RUN_TEST` call in the matching suite).
2. **Run the suite — it should fail.** `make test` confirms the test runs and fails for the right reason.
3. **Implement.** Add the declaration to the relevant `include/*.h` and the body to `src/*.c`.
4. **Re-run the suite — it should pass.** `make test` again.
5. **Memcheck.** `make memcheck` to confirm no leaks.

### Adding a runtime-tunable hyperparameter to a demo

The DQN trader and XOR demos accept positional `argv` overrides so the web UI can pass user-tweaked values straight to `Module.callMain([...])`. To expose a new parameter (e.g., a new flag for `market_generator`):

1. **C side:** Convert the existing `#define` to a local variable in `main()` and read `argv[N]` with `atoi`/`atof`, falling back to the default. Print "(overridden)" in the run header when the value differs from the compiled-in default — recruiters appreciate seeing what they actually ran.
2. **Worker:** `web/worker.js` already forwards `args` to `Module.callMain(args.map(String))` — no change needed.
3. **UI:** Add an `<input>` to the `tweak-panel` section of `web/index.html`. Register its id in `DEMOS[name].args` in `web/app.js` (the order must match the C-side `argv` parsing). Add the default to `TWEAK_DEFAULTS` so the reset button works.
4. **Tests:** Add an integration test in `tests/integration/run.sh` that runs the binary with the new flag and greps for the resolved value. If the UI surface is non-trivial, add a Gherkin scenario in `e2e/features/03-tweak-hyperparameters.feature`.

### Updating the web demo

The `web/` directory deploys as-is to Vercel. There's no build step — `index.html` imports its CSS and JS by relative path, and the asciinema-player + xterm libraries come from a CDN. To preview locally:

```bash
python3 -m http.server 8765 --directory web
# open http://localhost:8765/
```

If you change a C example *and* expect the web demo to pick it up, run `make wasm` and commit the regenerated `web/wasm/*.wasm` + `web/wasm/*.js`. CI's `wasm-build` job will warn if you forgot.

### Recording or re-recording an asciinema cast

```bash
# record (asciinema 3.x default is cast v3 — must convert for the player)
asciinema rec web/casts/example.cast --command "./example_bin"
asciinema convert -f asciicast-v2 web/casts/example.cast web/casts/example.v2.cast
mv web/casts/example.v2.cast web/casts/example.cast

# strip non-v2 events that asciinema-player 3.x doesn't understand
python3 -c "
import json, sys
with open('web/casts/example.cast') as f: lines = f.readlines()
header, events = lines[0], [l for l in lines[1:] if json.loads(l.strip())[1] in ('o','i','r','m')]
with open('web/casts/example.cast', 'w') as f:
    f.write(header); f.writelines(events)
"
```

Then bump `CAST_VERSION` in `web/app.js` so browsers fetch the new file instead of a cached one.

## Submitting changes

1. Fork the repo and create a topic branch from `main`: `git checkout -b feat/my-thing`.
2. Make your change. **Run `make test` and `make integration` before pushing.** Both must be green.
3. If you touched the web demo, also run `make wasm` (if you changed C) and `make e2e` (if you changed JS/HTML).
4. Open a PR. CI runs three jobs in parallel:
   - **c-suite** — `make test` + `make integration`
   - **wasm-build** — verifies `web/wasm/*` matches a fresh Emscripten build
   - **e2e** — Playwright + Gherkin BDD against a local web server
5. PRs need all three jobs green. Vercel deploys a preview URL for every PR — link it in your description if reviewers should see it live.

## A few specific things to keep in mind

- **No `Co-Authored-By: Claude` trailers** — commit messages attribute to the human author only.
- **Don't add config for hypothetical future contributors.** No `.editorconfig`, no `.prettierrc`, no GitHub issue templates, no PR template, no code-of-conduct file unless the project genuinely needs them. Three contributors is a low number to design for.
- **The unit-test count in `web/index.html` is checked manually.** If you add or remove tests, update `<strong>56</strong> unit tests passing` to match. There's no script for this — it's a deliberate forcing function to keep the page honest.
- **Don't introduce `data-testid` attributes for the E2E suite.** All locators in `e2e/steps/page.steps.ts` go through `getByRole` / `getByLabel` / `getByText`. If a test can't find an element by accessible name, fix the element's labelling instead.

## Reporting bugs

Open an issue with:
- The exact command you ran and the output (paste the full stderr/stdout, not a screenshot).
- Your `gcc --version` and OS.
- For web-demo bugs, the browser + version and any console errors.

## Questions

If something in this guide is wrong, unclear, or missing, that itself is a contribution worth filing.
