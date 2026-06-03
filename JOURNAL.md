# JOURNAL — qforge

> Dated log of decisions, pivots, incidents, and quotes. Add entries as
> things happen — retrospectives need this raw material to land.
> Reverse-chronological; one paragraph max per entry.

## 2026-06-03 — Dropped Homebrew's emscripten for the official emsdk #decision #incident

`brew doctor` started nagging about a deprecated `icu4c@77`, and the chain turned out to be `emscripten → yuicompressor → icu4c@77` — Homebrew's emscripten formula drags in yuicompressor for JS minification, and that's the only thing still pinning the old ICU. Rather than chase a different C→WASM toolchain (Zig, WASI SDK, bare Clang all looked tempting but would've meant rewriting ~240 `Module`/HEAP marshalling calls in the web demo for zero gain), the fix was just to swap the *source* of `emcc`: installed the official emsdk to `~/emsdk` and removed the brew formula, which let `brew autoremove` take yuicompressor and `icu4c@77` with it. Nice side effect — local now matches CI, which already builds via `mymindstorm/setup-emsdk`. Verified `make wasm` produces byte-identical artifacts before pulling the trigger.

## 2026-05-13 — Mermaid diagram wouldn't render on GitHub #incident

The architecture diagram looked fine locally but GitHub refused to render it. GitHub's Mermaid is stricter than most local previewers — it choked on syntax the editor had been forgiving about. Worth remembering that "renders in my editor" is not the bar; the bar is github.com.

## 2026-05-11 — Linux CI surfaced four bugs macOS had been hiding #incident

The day the web demo shipped, CI ran the native build on Ubuntu/gcc for the first time and immediately broke in four places — all of them latent the entire history of the project because macOS clang is lenient where glibc + `-std=c99 -pedantic` is not. `-lm` was placed *before* the source files, so GNU ld (left-to-right symbol resolution) threw libm away before it ever saw `exp`/`tanh`/`log`. `M_PI` (used in the Box-Muller sampler) is a POSIX extension glibc hides under `-std=c99`. So is `clock_gettime` in the benchmark. Fixed each at the source level rather than relaxing the global flags — a local `#ifndef M_PI` fallback and a file-scoped `_POSIX_C_SOURCE` instead of `-D_GNU_SOURCE` everywhere. Lesson banked: cross-compiler CI earns its keep; "works on my Mac" was masking real portability gaps.

## 2026-05-11 — The test harness had been reporting failures as passes #incident

The scariest find in the same CI shakeout: the test counters in `test_harness.h` were declared `static` at file scope, so every translation unit got its *own* private copy. An `ASSERT_FAIL` in `test_tensor.c` bumped test_tensor's counter, while `RUN_TEST` in `test_main.c` read test_main's always-zero copy. Net effect — a failing assertion could report as passed, and had been able to for the project's entire life. gcc's `-Werror=unused-variable` is what dragged it into the light. Fixed with the canonical `extern`-in-header + single-definition pattern. Genuinely unsettling that the green checkmarks meant less than we thought until this commit.

## 2026-05-11 — Shipped the in-browser WASM demo #milestone #decision

Stood up the `web/` landing site: every C example compiled to a ~40KB WASM module via Emscripten, each running in its own Web Worker with stdout piped through xterm.js, plus auto-playing asciinema recordings of the native binaries. The pitch is that a recruiter can run the exact same C — not a reimplementation — in their browser with one click, no install. Deployed as a pre-built static bundle on Vercel with cache headers for the WASM/cast assets. This is also where the project picked up its full repo baseline in one push: MIT license, CONTRIBUTING, theme-aware banner, badge row, OG/Twitter cards, Vercel Analytics + Speed Insights, and a three-layer test suite (unit + integration + E2E).

## 2026-04-06 — Built the whole framework in a day, test-first #milestone

The entire engine — tensors, activations, losses, dense layers, the sequential network, SGD-with-momentum — went in across a single day, each module landing with its tests alongside it (TDD throughout). On top of the core sat the four demos that define the project: an XOR sanity check, a performance benchmark, a numerical gradient verifier, a GARCH-style synthetic market generator, and the DQN trading agent that beats buy-and-hold. ~2,000 lines of C99, zero third-party dependencies, by design.
