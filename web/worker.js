/* ============================================================================
 * worker.js — Web Worker that loads an Emscripten module and runs main()
 *
 * Receives: { js: 'wasm/xor.js', exportName: 'createXor' }
 * Sends:    { type: 'stdout'|'stderr', text: '...' } and finally { type: 'done' }
 * ============================================================================ */

self.onmessage = async (e) => {
  const { js, exportName } = e.data;

  try {
    self.importScripts(js);

    const factory = self[exportName];
    if (typeof factory !== 'function') {
      throw new Error('Emscripten export "' + exportName + '" not found after loading ' + js);
    }

    const Module = await factory({
      print:    (text) => self.postMessage({ type: 'stdout', text }),
      printErr: (text) => self.postMessage({ type: 'stderr', text }),
      locateFile: (path) => {
        const dir = js.substring(0, js.lastIndexOf('/') + 1);
        return dir + path;
      },
      noInitialRun: true,
    });

    Module.callMain([]);
    self.postMessage({ type: 'done' });
  } catch (err) {
    self.postMessage({ type: 'error', message: String(err && err.message ? err.message : err) });
  }
};
