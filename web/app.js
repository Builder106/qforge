/* ============================================================================
 * app.js — main thread: asciinema-player tabs + xterm.js terminal + WASM runner
 *
 * The page respects the user's prefers-color-scheme: dark and light themes are
 * propagated through CSS variables, the asciinema-player theme prop, and the
 * xterm.js theme object. A media query listener re-creates the player and
 * re-themes the terminal when the OS preference flips at runtime.
 * ============================================================================ */

(function () {
  'use strict';

  /* ---- Theme detection ---- */

  const colorScheme = window.matchMedia('(prefers-color-scheme: dark)');
  const isDark = () => colorScheme.matches;

  /* asciinema-player ships these themes by name. solarized-light is the only
   * built-in light theme that reads well; the default "asciinema" theme is dark. */
  const asciinemaTheme = () => (isDark() ? 'asciinema' : 'solarized-light');

  /* Two xterm.js theme tables. The 16 ANSI slots are picked to match the
   * asciinema-player theme so ANSI escape sequences from the C demos render
   * the same way in both the cast playback and the live terminal. */
  const TERM_THEMES = {
    dark: {
      background:    '#000000',
      foreground:    '#e6e8eb',
      cursor:        '#7cc4ff',
      black:         '#0b0d10',
      red:           '#ff6b6b',
      green:         '#b3f5a8',
      yellow:        '#ffcc66',
      blue:          '#7cc4ff',
      magenta:       '#d4a8ff',
      cyan:          '#88d6c8',
      white:         '#e6e8eb',
      brightBlack:   '#5a6068',
      brightRed:     '#ff8c8c',
      brightGreen:   '#cdf5c5',
      brightYellow:  '#ffdb8c',
      brightBlue:    '#a3d5ff',
      brightMagenta: '#e4c4ff',
      brightCyan:    '#a8e0d4',
      brightWhite:   '#ffffff',
    },
    light: {
      /* Solarized Light, matching asciinema-player's solarized-light theme */
      background:    '#fdf6e3',
      foreground:    '#586e75',
      cursor:        '#268bd2',
      black:         '#073642',
      red:           '#dc322f',
      green:         '#859900',
      yellow:        '#b58900',
      blue:          '#268bd2',
      magenta:       '#d33682',
      cyan:          '#2aa198',
      white:         '#eee8d5',
      brightBlack:   '#002b36',
      brightRed:     '#cb4b16',
      brightGreen:   '#586e75',
      brightYellow:  '#657b83',
      brightBlue:    '#839496',
      brightMagenta: '#6c71c4',
      brightCyan:    '#93a1a1',
      brightWhite:   '#fdf6e3',
    },
  };

  /* ---- Asciinema player with tab switching + theme awareness ---- */

  const tabs = document.querySelectorAll('.tab');
  const playerContainer = document.getElementById('player');
  let currentPlayer = null;
  let currentTab = null;

  /* Cache-bust cast URLs so a stale v3 cast doesn't survive in the browser
   * cache after the source is regenerated. The cast files themselves don't
   * change between visits, but a deploy with new content needs to win. */
  const CAST_VERSION = '3';

  /* `playerVisible` is the latest signal from the IntersectionObserver; it may
   * flip before a freshly-created player is ready, so loadCast() also reads it
   * once the play() call is wired up. */
  let playerVisible = false;

  function loadCast(tab) {
    currentTab = tab;
    if (currentPlayer) {
      try { currentPlayer.dispose(); } catch (e) { /* older versions */ }
      playerContainer.innerHTML = '';
    }
    const castUrl = tab.dataset.cast + '?v=' + CAST_VERSION;
    currentPlayer = AsciinemaPlayer.create(castUrl, playerContainer, {
      cols: parseInt(tab.dataset.cols, 10) || 80,
      rows: parseInt(tab.dataset.rows, 10) || 24,
      speed: 1.5,
      idleTimeLimit: 1.5,
      poster: 'npt:0:00',
      theme: asciinemaTheme(),
      fit: 'width',
      loop: true,
    });

    /* Autoplay if the player is on-screen at creation time (e.g. tab switch
     * while the section is in view). play() returns a Promise that can reject
     * if the cast hasn't loaded yet — swallow that and rely on the observer
     * to retry once the section becomes visible again. */
    if (playerVisible) {
      try {
        const p = currentPlayer.play();
        if (p && typeof p.catch === 'function') p.catch(() => {});
      } catch (e) { /* not ready */ }
    }
  }

  /* IntersectionObserver: play when the player scrolls into view, pause when
   * it scrolls out. Threshold 0.25 means a quarter of the player must be
   * visible — avoids triggering when only a sliver pokes into the viewport. */
  const playerObserver = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      playerVisible = entry.isIntersecting;
      if (!currentPlayer) continue;
      try {
        if (entry.isIntersecting) {
          const p = currentPlayer.play();
          if (p && typeof p.catch === 'function') p.catch(() => {});
        } else {
          currentPlayer.pause();
        }
      } catch (e) { /* player not ready or already in target state */ }
    }
  }, { threshold: 0.25 });
  playerObserver.observe(playerContainer);

  tabs.forEach((tab) => {
    tab.addEventListener('click', () => {
      tabs.forEach((t) => t.classList.remove('tab-active'));
      tab.classList.add('tab-active');
      loadCast(tab);
    });
  });

  const initialTab = document.querySelector('.tab-active') || tabs[0];
  if (initialTab) loadCast(initialTab);

  /* ---- xterm.js terminal ---- */

  const term = new Terminal({
    fontFamily: 'ui-monospace, "SF Mono", "JetBrains Mono", Menlo, Consolas, monospace',
    fontSize: 13,
    lineHeight: 1.2,
    cursorBlink: false,
    cursorStyle: 'block',
    disableStdin: true,
    convertEol: true,
    scrollback: 5000,
    theme: isDark() ? TERM_THEMES.dark : TERM_THEMES.light,
  });

  term.open(document.getElementById('terminal'));

  function fitTerminalToContainer() {
    /* Rough heuristic: estimate cols from container width, since we don't
     * pull in the FitAddon. Each cell is ~7.8px wide at 13px font size. */
    const containerWidth = document.getElementById('terminal').clientWidth;
    const cols = Math.max(60, Math.floor((containerWidth - 16) / 7.8));
    const rows = 32;
    try { term.resize(cols, rows); } catch (e) { /* ignore */ }
  }
  fitTerminalToContainer();
  window.addEventListener('resize', fitTerminalToContainer);

  /* Idle banner: a muted preview of what's about to happen so the empty
   * terminal state isn't a black void. ANSI 90 = bright black (grey) keeps
   * it visually quiet next to the real demo output that follows. */
  const IDLE_BANNER = [
    '',
    '\x1b[90m  ╔══════════════════════════════════════════════════════════════╗\x1b[0m',
    '\x1b[90m  ║                                                              ║\x1b[0m',
    '\x1b[90m  ║   qforge — \x1b[36min-browser WASM runtime\x1b[90m                          ║\x1b[0m',
    '\x1b[90m  ║                                                              ║\x1b[0m',
    '\x1b[90m  ║   Same C99 source, compiled to WebAssembly.                  ║\x1b[0m',
    '\x1b[90m  ║   Every demo runs in a Web Worker — output streams here.     ║\x1b[0m',
    '\x1b[90m  ║                                                              ║\x1b[0m',
    '\x1b[90m  ╚══════════════════════════════════════════════════════════════╝\x1b[0m',
    '',
    '\x1b[90m  $ \x1b[0m\x1b[37m_\x1b[0m   \x1b[90m  ← pick a demo from the buttons above\x1b[0m',
    '',
  ];
  IDLE_BANNER.forEach((line) => term.writeln(line));

  /* Re-theme on OS-level color-scheme flip. Asciinema-player has no `setTheme`
   * API, so we re-create it on the current tab; xterm.js exposes `options.theme`
   * which swaps live. */
  colorScheme.addEventListener('change', () => {
    term.options.theme = isDark() ? TERM_THEMES.dark : TERM_THEMES.light;
    if (currentTab) loadCast(currentTab);
  });

  /* ---- Demo runner: spawn a Web Worker per run ---- */

  const DEMOS = {
    xor:            { js: 'wasm/xor.js',            exportName: 'createXor',           label: 'XOR' },
    benchmark:      { js: 'wasm/benchmark.js',      exportName: 'createBenchmark',     label: 'benchmark' },
    dqn:            { js: 'wasm/dqn.js',            exportName: 'createDqn',           label: 'DQN trader' },
    market_gen:     { js: 'wasm/market_gen.js',     exportName: 'createMarketGen',     label: 'market generator' },
    gradient_check: { js: 'wasm/gradient_check.js', exportName: 'createGradientCheck', label: 'gradient check' },
  };

  const runButtons = document.querySelectorAll('.btn-run');
  const clearBtn = document.getElementById('clear-term');
  const hint = document.getElementById('hint');
  let activeWorker = null;

  function setButtonsDisabled(disabled) {
    runButtons.forEach((b) => { b.disabled = disabled; });
  }

  function runDemo(name) {
    const demo = DEMOS[name];
    if (!demo) return;

    if (activeWorker) {
      activeWorker.terminate();
      activeWorker = null;
    }

    term.writeln('\x1b[90m$ ./' + name + '_bin\x1b[0m');
    setButtonsDisabled(true);
    hint.textContent = 'Running ' + demo.label + ' — output streams below.';

    const t0 = performance.now();
    const worker = new Worker('worker.js');
    activeWorker = worker;

    worker.onmessage = (e) => {
      const msg = e.data;
      if (msg.type === 'stdout') {
        term.writeln(msg.text);
      } else if (msg.type === 'stderr') {
        term.writeln('\x1b[31m' + msg.text + '\x1b[0m');
      } else if (msg.type === 'done') {
        const elapsed = ((performance.now() - t0) / 1000).toFixed(2);
        term.writeln('\x1b[90m[exited in ' + elapsed + 's]\x1b[0m');
        term.writeln('');
        setButtonsDisabled(false);
        hint.textContent = '↑ Pick another demo, or run the same one again with a fresh seed.';
        worker.terminate();
        if (activeWorker === worker) activeWorker = null;
      } else if (msg.type === 'error') {
        term.writeln('\x1b[31m[error] ' + msg.message + '\x1b[0m');
        term.writeln('');
        setButtonsDisabled(false);
        hint.textContent = '↑ Something went wrong. Check the browser console for details.';
        worker.terminate();
        if (activeWorker === worker) activeWorker = null;
      }
    };

    worker.onerror = (err) => {
      term.writeln('\x1b[31m[worker error] ' + (err.message || err) + '\x1b[0m');
      term.writeln('');
      setButtonsDisabled(false);
      worker.terminate();
      if (activeWorker === worker) activeWorker = null;
    };

    worker.postMessage({ js: demo.js, exportName: demo.exportName });
  }

  runButtons.forEach((btn) => {
    btn.addEventListener('click', () => runDemo(btn.dataset.demo));
  });

  clearBtn.addEventListener('click', () => {
    term.clear();
  });

})();
