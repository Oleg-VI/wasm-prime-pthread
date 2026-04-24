# WASM Prime Generator — Multithreaded (Emscripten pthreads)

A WebAssembly prime-number service built in **C++20**, compiled with Emscripten,
and exposed to JavaScript via **embind**. Computation runs in a true background
**Web Worker** thread via Emscripten's pthreads support, keeping the browser UI
completely non-blocking at all times.

---

## What it does

Finds all prime numbers below a user-supplied limit using the
**Sieve of Eratosthenes**. Progress is reported continuously to the UI during
computation. The page remains fully interactive throughout — the progress bar
updates in real time and the Cancel button responds instantly, even during
allocation of multi-gigabyte sieves.

---

## Algorithm

### Sieve of Eratosthenes

1. Allocate a `std::vector<bool>` of size `limit + 1`, initialised to `true`.
2. For every factor `f` from 2 to `√limit`: if `sieve[f]` is still `true`,
   mark all multiples `f², f²+f, f²+2f, …` as composite.
3. Collect all indices still marked `true` — these are the primes.

Time complexity: **O(n log log n)**. Space: **O(n)** bits (one bit per element
thanks to `std::vector<bool>`).

---

## How WASM + Pthreads works here

```
Browser Main Thread
│
├─ JS loads prime_module.wasm via ES6 import
│  (Emscripten pre-spawns a Worker pool: PTHREAD_POOL_SIZE=2)
│
├─ User clicks Start
│   └─ PrimeService.startComputation()
│       └─ C++ PrimeGenerator::StartComputation()
│           └─ std::thread([]{...}).detach()  ← maps to a Web Worker
│
│                        Web Worker Thread
│                        │
│                        ├─ sieve.assign()       // allocate ~1 GB — does NOT block UI
│                        ├─ sieve marking loop
│                        │   ├─ on_progress(%) ──┐
│                        │   └─ (check cancel)   │ emscripten_async_run_in_main_runtime_thread()
│                        │                       │
│                        └─ on_complete(primes) ─┘
│
├─ Main thread receives dispatched callbacks, calls JS functions
│
└─ UI events handled freely at all times — worker never touches the DOM
```

### Key insight

`std::thread` in Emscripten compiles directly to a **Web Worker**. The worker
has its own memory view into the shared WASM linear memory (via
`SharedArrayBuffer`). The main thread never blocks — not during allocation,
not during sieve computation, not during result collection.

JS callbacks (`on_progress`, `on_complete`) are dispatched back to the main
thread via `emscripten_async_run_in_main_runtime_thread()` because JavaScript
objects (`emscripten::val`) may only be accessed from the thread that owns them.

**Requires `SharedArrayBuffer`**, which in turn requires the following HTTP
headers on the server (already configured in `web/server.py`):
```
Cross-Origin-Opener-Policy:   same-origin
Cross-Origin-Embedder-Policy: require-corp
```

---

## Technical decisions

| Decision | Choice | Rationale |
|---|---|---|
| Threading model | `std::thread` → Web Worker via Emscripten pthreads | Standard C++; zero WASM-specific threading code in the core class |
| No `PROXY_TO_PTHREAD` | Omitted | Requires a `main()` entry point; incompatible with an embind-only library |
| JS callback dispatch | `emscripten_async_run_in_main_runtime_thread` | `emscripten::val` is not thread-safe; must be called from the thread that owns the JS context |
| `val` stored in adapter, not passed to worker | `PrimeGeneratorAdapter` members | `val` cannot be safely copied across thread boundaries; only plain `this` pointer is captured in the lambda |
| Zero-copy result transfer | `typed_memory_view` → `Int32Array.new_()` | Avoids element-by-element copy of millions of integers; single `memcpy`-level bulk transfer |
| `std::vector<bool>` | 1 bit per element | 8× less memory vs `vector<char>`; critical at multi-gigabyte scales |
| `thread_.join()` before re-use | Called at start of `StartComputation` | Prevents `std::terminate()` from assigning to a joinable thread after the previous computation finishes |
| Cancel check before collect phase | `cancelled_.load()` after sieve loop | Avoids the expensive `std::count` + vector fill + cross-thread dispatch if the user already cancelled |
| No exceptions | `[[nodiscard]]`, `bool` return codes | Consistent with Emscripten best practices; lighter binary |
| PascalCase exports | `PrimeGenerator`, `StartComputation`, … | Requirement; isolated in `bindings.cpp` adapter so core class is unaffected |
| camelCase JS API | `PrimeService.startComputation()`, … | Requirement; isolated in `prime_service.js` |

---

## Limitations

| Limitation | Cause |
|---|---|
| Requires COOP/COEP headers | `SharedArrayBuffer` is gated behind cross-origin isolation by browsers (Spectre mitigation) |
| `int` limit (~2.1 B max) | Intentional; widening to `long long` would require sieve memory beyond practical WASM limits |
| Brief pause at ~99% for very large limits | `std::count` over `vector<bool>` + result collection still runs in the worker but `Int32Array` construction on the main thread takes ~1 s at 1 B limit |
| Worker pool pre-allocation | `PTHREAD_POOL_SIZE=2` spawns Workers at module load; negligible overhead but visible in DevTools |

---

## Pros and cons

**Pros**
- UI is **never blocked** — not during allocation, computation, or result transfer.
- C++ core is clean, standard C++20 — no Emscripten-specific async primitives.
- Cancel is truly immediate at any point during computation.
- Scales naturally: worker runs at full CPU speed with no yield overhead.

**Cons**
- Requires COOP/COEP HTTP headers — cannot be served from an arbitrary CDN or
  GitHub Pages without additional configuration.
- `SharedArrayBuffer` must be available in the browser (all modern browsers
  support it, but it can be disabled by enterprise policy).
- Slightly more complex `bindings.cpp` due to cross-thread callback dispatch.

---

## Prerequisites

### 1. Git
Download from [git-scm.com](https://git-scm.com). Required to clone emsdk.

### 2. Python 3.x
From [python.org](https://python.org). Required by emsdk and the dev server.
**Add to PATH during installation.**

### 3. CMake 3.20+
```
winget install Kitware.CMake
```

### 4. Ninja
```
winget install Ninja-build.Ninja
```

### 5. Emscripten SDK
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
emsdk install latest
emsdk activate latest
# Activate the environment (required in every new terminal):
emsdk_env.bat        # cmd
.\emsdk_env.ps1      # PowerShell
```

Visual Studio / MSVC is **not required** — emsdk ships its own Clang/LLD
toolchain.

---

## Build

```batch
# 1. Activate emsdk (every new terminal)
cd C:\emsdk
emsdk_env.bat

# 2. Go to the project and build
cd path\to\wasm-prime-pthread
build.bat
```

Output files appear in `web\`:
```
web/
├── prime_module.js         ← Emscripten JS glue
├── prime_module.wasm       ← compiled WebAssembly module
├── index.html
├── prime_service.js
└── server.py
```

---

## Run

```batch
python web\server.py
```

Open **http://localhost:8080** in any modern browser.

> **Note:** the dev server at `web/server.py` already sends the required
> `Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers.
> If you deploy elsewhere, these headers must be configured on your web server.

---

## Project structure

```
wasm-prime-pthread/
├── CMakeLists.txt
├── build.bat
├── src/
│   ├── prime_generator.h      # PrimeGenerator class declaration
│   ├── prime_generator.cpp    # Sieve + std::thread logic
│   └── bindings.cpp           # embind adapter + cross-thread callback dispatch
└── web/
    ├── index.html             # UI
    ├── prime_service.js       # JS wrapper (camelCase API)
    └── server.py              # Dev server with COOP/COEP headers
```

---

## Comparison with the cooperative async variant

| | `emscripten_async_call` | `pthreads` (this repo) |
|---|---|---|
| True parallelism | ❌ cooperative | ✅ real Worker thread |
| UI freeze during large malloc | ✅ yes | ❌ never |
| Requires COOP/COEP headers | ❌ no | ✅ yes |
| C++ core complexity | Higher (chunking logic) | Lower (plain loop) |
| `bindings.cpp` complexity | Lower | Higher (thread dispatch) |
| Deploy to GitHub Pages as-is | ✅ yes | ❌ needs header config |
