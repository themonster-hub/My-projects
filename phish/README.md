# Phish Chess Engine (Experimental)

Phish is a modern chess engine project targeting strong, fast play with a clean architecture. This repository currently contains an experimental prototype: correct move generation, UCI protocol, basic search with transposition table and pruning, and a small perft harness.

Status: Experimental. Interfaces and internals will change. Strength is not tuned and is far below top engines.

## Features (current)
- C++20 codebase, CMake build
- Bitboards with precomputed attacks for king/knight/pawns; simple sliding attacks
- Legal move generation and FEN parsing
- Zobrist hashing and exact make/unmake (incl. EP, castling, promotion)
- UCI protocol: position/go/perft/setoption
- Search skeleton: iterative deepening, PVS, TT, null-move pruning, simple material eval
- Perft tool and basic test list (startpos depths 1–3)

## Requirements
- CMake ≥ 3.16
- A C++20 compiler (clang ≥ 12, or gcc ≥ 11 recommended)
- Linux/macOS (Windows/MSVC support planned)

Optional: LTO/IPO enabled by default (PHISH_ENABLE_LTO=ON). Disable with -DPHISH_ENABLE_LTO=OFF if toolchain/linker has issues.

## Build
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
go depth 5
```

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

## Project layout
```
phish/
 ├─ engine/
 │   ├─ bitboard/      # attack tables, sliding attacks
 │   ├─ board/         # Position, make/unmake, FEN
 │   ├─ movegen/       # moves + encoding
 │   ├─ search/        # PVS/TT/null-move (experimental)
 │   ├─ uci/           # UCI loop
 │   └─ util/          # config, types, zobrist
 └─ tests/
     └─ perft/         # perft tool + positions
```

## Roadmap (high level)
- Correctness: expand perft suite; pins/check evasions edge cases; fuzzing
- Strength: SEE, LMR/LMP, futility/razoring/IID, killer/history/countermove heuristics
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