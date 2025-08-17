# Phish Chess Engine (Experimental)

Phish is a modern chess engine project targeting strong, fast play with a clean architecture. This repository currently contains an experimental prototype: correct move generation, UCI protocol, basic search with transposition table and pruning, and a small perft harness.

Status: Experimental. Interfaces and internals will change. Strength is not tuned and is far below top engines.

## Features (current)
- C++20 codebase, CMake build
- Bitboards with precomputed attacks for king/knight/pawns; simple sliding attacks
- Legal move generation and FEN parsing
- Zobrist hashing and exact make/unmake (incl. EP, castling, promotion)
- UCI protocol: position/go/perft/setoption, async search with proper stop handling and periodic info lines
- Search skeleton: iterative deepening, PVS, TT, null-move pruning, killers/history heuristics, basic capture-only quiescence, LMR/LMP/razoring/static-prune, MVV-LVA and countermoves
- Perft tool and basic test list (startpos depths 1–3)

## Requirements
- CMake ≥ 3.16
- A C++20 compiler (clang ≥ 12, or gcc ≥ 11 recommended)
- Linux/macOS (Windows/MSVC support planned)

Optional: LTO/IPO enabled by default (PHISH_ENABLE_LTO=ON). Disable with -DPHISH_ENABLE_LTO=OFF if toolchain/linker has issues.

## Build (Desktop)
```
cmake -S /workspace/phish -B /workspace/phish/build -DCMAKE_BUILD_TYPE=Release
cmake --build /workspace/phish/build -j
```
The engine binary will be at:
```
/workspace/phish/build/phish
```

## Run (UCI)
Phish speaks UCI. Example:
```
/workspace/phish/build/phish
uci
isready
position startpos
go depth 6
```
Example with a short line:
```
position startpos moves e2e4 e7e5 g1f3 b8c6
go wtime 30000 btime 30000 winc 0 binc 0
```

Phish supports: `go depth N`, `go movetime T`, `go wtime/btime[/winc/binc]`, `go nodes N`, and `go infinite` with `stop`.

Supported UCI options (subset):
- Hash (MB)
- Threads (placeholder; SMP not yet implemented)
- Ponder (placeholder)
- SyzygyPath, SyzygyProbeDepth (placeholders)
- UseNNUE, EvalFile (placeholders)
- Contempt
- MoveOverhead
- MultiPV (placeholder)

## Perft tests
A tiny perft harness is included.

Run with default list:
```
/workspace/phish/build/tests/phish_perft /workspace/phish/tests/perft/perft_positions.txt
```
Edit `tests/perft/perft_positions.txt` to add more FENs and depths:
```
<fen or startpos>;<depth>;<expected_nodes>
```

## Mobile (Android) — How to install and run
There are two common ways to use a UCI engine on Android. Choose 1 (recommended) or 2.

1) Use DroidFish (or Chess for Android) and copy the prebuilt binary
- Install apps:
  - DroidFish from Google Play
  - Alternatively: Chess for Android (also supports UCI engines)
- Get a Phish ARM64 binary:
  - If you have a desktop: build for Android (arm64) and copy to phone (see "Build for Android" below).
  - Or use Termux on Android to build directly on device (see option 2).
- In DroidFish: Menu → Manage Chess Engines → New UCI engine → Select the `phish` binary you copied to phone storage. You may need to allow execute permission.
- Start a game; pick Phish as engine. Phish prints periodic UCI `info` lines and responds to `stop`.

2) Build on phone using Termux (no PC required)
- Install apps:
  - Termux from F-Droid
  - Optional GUIs: DroidFish or Chess for Android
- In Termux, install toolchain:
```
pkg update
pkg install git cmake clang
```
- Clone the project:
```
git clone https://github.com/themonster-hub/My-projects.git
cd My-projects/phish
```
- Build (Release):
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DPHISH_ENABLE_LTO=OFF
cmake --build build -j$(nproc)
```
- The binary: `./build/phish`
- Option A: Run in Termux directly and drive via UCI.
- Option B: Copy `build/phish` into a folder accessible to DroidFish/Chess for Android and add it as a UCI engine as in option 1.

### Build for Android (with Android NDK)
If you prefer cross-compiling from PC for Android (arm64):
```
cmake -S . -B build-android \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DPHISH_ENABLE_LTO=OFF
cmake --build build-android -j
```
Copy `build-android/phish` to your phone.

Notes:
- Threads option is present but multi-threaded search is not implemented yet.
- NEON/NNUE acceleration is not implemented yet; future work.
- If the binary does not run, ensure it has execute permission and matches your device architecture (arm64-v8a).

## Known issues and notes
- PV in UCI `info` is currently minimal (root move only). Full PV extraction is planned.
- SMP not implemented; `Threads` is a placeholder.
- Syzygy/NNUE options are placeholders (not functional yet).
- We print `bestmove` once per search. When `stop` is issued, we print a final best move computed so far.
- If you see extremely fast `depth 1` info only: the GUI might be calling `go` with very short time limits; try `go depth N` to verify deeper search.

## Project layout
```
phish/
 ├─ engine/
 │   ├─ bitboard/      # attack tables, sliding attacks
 │   ├─ board/         # Position, make/unmake, FEN
 │   ├─ movegen/       # moves + encoding
 │   ├─ search/        # PVS/TT/null-move/etc.
 │   ├─ uci/           # UCI loop
 │   └─ util/          # config, types, zobrist
 └─ tests/
     └─ perft/         # perft tool + positions
```

## Roadmap (high level)
- Correctness: expand perft suite; pins/check evasions edge cases; fuzzing
- Strength: SEE, more robust LMR/LMP, futility/razoring/IID tuning, countermove/CMH refinement
- NNUE: HalfKP/KxP features, incremental updates, SIMD inference (AVX2/NEON)
- Time management and pondering
- SMP (Lazy SMP), lock-free clustered TT
- Syzygy WDL/DTZ probing
- Training + A/B testing (SPRT)

## Version
- 0.1.0 (experimental)

## Notes
- This is a research/prototype engine. Expect rapid iteration and breaking changes.
- Performance flags: release builds use -O3; LTO is enabled if supported.