# campello_audio — WASM Example

This is a minimal browser example that demonstrates the WebAssembly backend.

## Prerequisites

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (3.1.45 or newer)
- A local HTTP server that can set COOP/COEP headers

## Build

From the project root:

```bash
emcmake cmake -B build-wasm \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=ON

cmake --build build-wasm
```

The example outputs will be in `build-wasm/examples/wasm/`:
- `wasm_example.js` — Emscripten JS loader
- `wasm_example.wasm` — WebAssembly binary
- `wasm_example.ww.js` — WASM worker for pthreads
- `index.html` — This HTML page (copy it there manually or serve from this directory)

## Run

Because the WASM backend uses `SharedArrayBuffer` and pthreads, the page must be served with specific cross-origin isolation headers:

```bash
# Using Python (3.x)
python3 -m http.server 8000
```

Then open `http://localhost:8000/examples/wasm/index.html` in a browser.

**Important:** For SharedArrayBuffer to work, the server MUST send these headers:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

You can use a simple Python server with headers:

```python
# serve.py
from http.server import HTTPServer, SimpleHTTPRequestHandler

class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

HTTPServer(("", 8000), Handler).serve_forever()
```

Then run `python3 serve.py` and open `http://localhost:8000/examples/wasm/index.html`.

## What to Expect

1. The page loads and initializes the Emscripten module.
2. Click **Start Audio** — this calls `AudioEngine::init()` and plays a 440 Hz sine tone.
3. Click **Stop Audio** — this calls `AudioEngine::deinit()`.

The browser console will show status messages from the C++ code.
