#!/usr/bin/env bash
# ============================================================================
# tests/integration/run.sh — black-box tests of compiled example binaries.
#
# Runs each example with realistic argv combinations and asserts on stdout:
# convergence markers, table headers, statistical properties, etc. Catches
# "the demo is broken" before a deploy.
#
# Usage:  ./tests/integration/run.sh           — run all
#         ./tests/integration/run.sh xor       — run a single group
# ============================================================================

set -u

# Locate repo root regardless of where the script is invoked from
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

PASS=0
FAIL=0
declare -a FAILURES=()

RED='\033[1;31m'; GREEN='\033[1;32m'; YELLOW='\033[1;33m'; CYAN='\033[1;36m'; RESET='\033[0m'

# ---- Assertion helpers ----

assert_grep() {
  local name="$1"; local pattern="$2"; local output="$3"
  if printf '%s' "$output" | grep -qE "$pattern"; then
    PASS=$((PASS+1))
    printf "  ${GREEN}PASS${RESET} %s\n" "$name"
  else
    FAIL=$((FAIL+1))
    FAILURES+=("$name (pattern: $pattern)")
    printf "  ${RED}FAIL${RESET} %s\n         missing pattern: %s\n" "$name" "$pattern"
  fi
}

assert_not_grep() {
  local name="$1"; local pattern="$2"; local output="$3"
  if printf '%s' "$output" | grep -qE "$pattern"; then
    FAIL=$((FAIL+1))
    FAILURES+=("$name (unexpected pattern: $pattern)")
    printf "  ${RED}FAIL${RESET} %s\n         unexpected pattern present: %s\n" "$name" "$pattern"
  else
    PASS=$((PASS+1))
    printf "  ${GREEN}PASS${RESET} %s\n" "$name"
  fi
}

assert_exit_ok() {
  local name="$1"; local rc="$2"
  if [ "$rc" -eq 0 ]; then
    PASS=$((PASS+1))
    printf "  ${GREEN}PASS${RESET} %s (exit=0)\n" "$name"
  else
    FAIL=$((FAIL+1))
    FAILURES+=("$name (exit=$rc)")
    printf "  ${RED}FAIL${RESET} %s (exit=%d)\n" "$name" "$rc"
  fi
}

suite() {
  printf "\n${YELLOW}━━━ %s ━━━${RESET}\n" "$1"
}

# ---- Binary build check ----

ensure_built() {
  local missing=()
  for bin in xor_example dqn_trader_bin market_gen_bin benchmark gradient_check; do
    if [ ! -x "./$bin" ]; then
      missing+=("$bin")
    fi
  done
  if [ ${#missing[@]} -gt 0 ]; then
    printf "${YELLOW}[build]${RESET} rebuilding missing: %s\n" "${missing[*]}"
    for bin in "${missing[@]}"; do
      make "$bin" >/dev/null 2>&1 || {
        printf "${RED}[build]${RESET} failed to build %s\n" "$bin"
        exit 2
      }
    done
  fi
}

# ---- XOR: convergence + argv override ----

test_xor() {
  suite "XOR — convergence + argv overrides"
  local output rc

  # Default-ish run (small to stay fast); should still converge.
  # XOR table uses Unicode box-drawing characters (│), not ASCII pipes —
  # match on `[in1, in2]` and the prediction value loosely on the same line.
  output=$(./xor_example 5000 1.0 2>&1); rc=$?
  assert_exit_ok "xor: 5000 epochs, lr=1.0" $rc
  assert_grep   "header echoes 5000 epochs"      'Epochs:[[:space:]]+5000' "$output"
  assert_grep   "final loss < 0.01"              'Epoch[[:space:]]+5000.*Loss:[[:space:]]0\.00' "$output"
  assert_grep   "predicts [0,0] near 0"          '\[0, 0\].*[[:space:]]0\.0[0-9]+' "$output"
  assert_grep   "predicts [0,1] near 1"          '\[0, 1\].*[[:space:]]0\.9[0-9]+' "$output"
  assert_grep   "predicts [1,0] near 1"          '\[1, 0\].*[[:space:]]0\.9[0-9]+' "$output"
  assert_grep   "predicts [1,1] near 0"          '\[1, 1\].*[[:space:]]0\.0[0-9]+' "$output"

  # Tiny lr should not diverge to NaN
  output=$(./xor_example 200 0.1 2>&1); rc=$?
  assert_exit_ok    "xor: small lr completes"    $rc
  assert_not_grep   "xor: no NaN output"          'nan|NaN|inf' "$output"
}

# ---- DQN: argv override, convergence shape, baseline comparison ----

test_dqn() {
  suite "DQN trader — argv overrides + episode shape"
  local output rc

  # 30 episodes for speed; verify the header reflects the override and the
  # binary completes a full evaluation including the buy-and-hold baseline.
  output=$(./dqn_trader_bin 30 0.99 0.001 0.99 2>&1); rc=$?
  assert_exit_ok "dqn: 30 ep, γ=0.99, lr=0.001, ε-decay=0.99" $rc
  assert_grep    "header shows γ=0.990"               'Gamma:[[:space:]]+0\.990' "$output"
  assert_grep    "γ marked as overridden"             'overridden' "$output"
  assert_grep    "header shows 30 episodes"           'Episodes:[[:space:]]+30 × 200 steps' "$output"
  assert_grep    "training table renders"             'Episode.*Total P&L.*Avg Reward' "$output"
  assert_grep    "evaluation block runs"              'Evaluation' "$output"
  assert_grep    "buy-and-hold baseline shown"        'Buy & Hold P&L' "$output"
  assert_grep    "final cleanup line"                 'Zero memory leaks' "$output"
  assert_not_grep "no NaN in P&L"                     'P&L.*nan' "$output"
}

# ---- Market generator: stylized facts table ----

test_market_gen() {
  suite "Market generator — stylized facts"
  local output rc

  output=$(./market_gen_bin 2>&1); rc=$?
  assert_exit_ok "market_gen: completes" $rc
  assert_grep    "stylized fact: mean"            'Mean daily return' "$output"
  assert_grep    "stylized fact: std dev"         'Std deviation' "$output"
  assert_grep    "stylized fact: skewness"        'Skewness' "$output"
  assert_grep    "stylized fact: kurtosis"        'Excess kurtosis' "$output"
  assert_grep    "stylized fact: vol clustering"  'Autocorr\(r²\) lag-1' "$output"
  # The kurtosis check line is reported regardless of pass/fail — synthetic
  # kurtosis fluctuates run-to-run (random Box-Muller noise), so we only
  # verify the framework prints the diagnostic, not the specific outcome.
  assert_grep    "kurtosis diagnostic emitted"    'Fat tails \(kurtosis > 0\):.*real=' "$output"
  assert_not_grep "no NaN in stats"               'Real Data.*nan|Synthetic.*nan' "$output"
}

# ---- Benchmark: all matmul sizes complete ----

test_benchmark() {
  suite "Benchmark — all sizes complete"
  local output rc

  output=$(./benchmark 2>&1); rc=$?
  assert_exit_ok "benchmark: completes" $rc
  for n in 32 64 128 256 512; do
    assert_grep "matmul ${n}×${n} reported" "matmul[[:space:]]+${n}×${n}.*MFLOP/s" "$output"
  done
  assert_grep "training step measured (16→32→1)"  'train[[:space:]]+16→32→32→1' "$output"
  assert_grep "training step measured (256→512→32)" 'train[[:space:]]+256→512→512→32' "$output"
}

# ---- Gradient check: all 4 architectures pass ----

# The example's output spans multiple lines per architecture: a "── Test N"
# header followed by "Layer X weights/biases: K/K passed" lines. Match each
# of those across the whole output (not per line) — and assert no failures
# anywhere in the report.

assert_block_passed() {
  local name="$1"; local header_re="$2"; local output="$3"
  # Header must appear, AND there must be no "FAIL" in the whole report.
  if printf '%s' "$output" | grep -qE "$header_re" && \
     ! printf '%s' "$output" | grep -qE 'FAIL|fail'; then
    PASS=$((PASS+1))
    printf "  ${GREEN}PASS${RESET} %s\n" "$name"
  else
    FAIL=$((FAIL+1))
    FAILURES+=("$name")
    printf "  ${RED}FAIL${RESET} %s\n" "$name"
  fi
}

test_gradient_check() {
  suite "Gradient check — all architectures pass"
  local output rc

  output=$(./gradient_check 2>&1); rc=$?
  assert_exit_ok       "gradient_check: completes"          $rc
  assert_block_passed  "test 1 header present (sigmoid stack)"     'Test 1.*sigmoid.*sigmoid' "$output"
  assert_block_passed  "test 2 header present (relu→relu→sig)"     'Test 2.*relu.*relu.*sigmoid' "$output"
  assert_block_passed  "test 3 header present (linear)"            'Test 3.*Network' "$output"
  assert_block_passed  "test 4 header present (tanh→sigmoid)"      'Test 4.*tanh.*sigmoid' "$output"
  assert_grep          "summary line: all passed"                  'All gradient checks passed' "$output"
}

# ---- Driver ----

main() {
  printf "${CYAN}╔══════════════════════════════════════════════════════════════╗${RESET}\n"
  printf "${CYAN}║   qforge — integration tests (compiled binaries)             ║${RESET}\n"
  printf "${CYAN}╚══════════════════════════════════════════════════════════════╝${RESET}\n"

  ensure_built

  local group="${1:-all}"
  case "$group" in
    all)
      test_xor; test_dqn; test_market_gen; test_benchmark; test_gradient_check ;;
    xor)            test_xor ;;
    dqn)            test_dqn ;;
    market_gen)     test_market_gen ;;
    benchmark)      test_benchmark ;;
    gradient_check) test_gradient_check ;;
    *)              printf "${RED}Unknown group: %s${RESET}\n" "$group"; exit 2 ;;
  esac

  printf "\n${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RESET}\n"
  printf "  Total: %d  |  ${GREEN}Passed: %d${RESET}  |  " $((PASS+FAIL)) "$PASS"
  if [ "$FAIL" -gt 0 ]; then
    printf "${RED}Failed: %d${RESET}\n" "$FAIL"
    printf "\n${RED}Failures:${RESET}\n"
    for f in "${FAILURES[@]}"; do
      printf "  • %s\n" "$f"
    done
    printf "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RESET}\n\n"
    exit 1
  else
    printf "${GREEN}Failed: 0${RESET}\n"
    printf "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RESET}\n"
    printf "  ${GREEN}✓ All integration tests passed.${RESET}\n\n"
  fi
}

main "$@"
