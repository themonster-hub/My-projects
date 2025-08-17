# Phish Chess Engine (Experimental)

Phish is a modern chess engine targeting strong and fast play with a clean architecture. This repository currently contains an experimental prototype: correct move generation, UCI protocol, basic search with TT/pruning, and a small perft harness.

Status: Experimental. Interfaces and internals change quickly. Strength is not tuned.

## Features (current)
- C++20 codebase, CMake build
- Bitboards (precomputed attacks), legal movegen, FEN parsing
- Zobrist hashing; exact make/unmake (EP, castling, promotion)
- UCI: position/go/perft/setoption
- Search: iterative deepening, PVS, TT, null-move pruning, basic material eval
- Perft harness and tests (startpos depths 1–3)

## Install and Setup
### Requirements
- CMake ≥ 3.16
- C++20 compiler (clang ≥ 12 or gcc ≥ 11 recommended)
- Linux/macOS. Windows is possible with MSVC; Android below.

### Build (Release)
```
cmake -S /workspace/phish -B /workspace/phish/build -DCMAKE_BUILD_TYPE=Release
cmake --build /workspace/phish/build -j
```
Engine binary:
```
/workspace/phish/build/phish
```

Optional flags:
- Enable/disable LTO/IPO:
```
-DPHISH_ENABLE_LTO=ON|OFF
```
- Enable aggressive `-Ofast` (may break strict IEEE):
```
-DPHISH_ENABLE_OFAST=ON
```

### Run (UCI)
Run in a terminal or any UCI GUI:
```
/workspace/phish/build/phish
uci
isready
position startpos
go depth 8
```
A short line example:
```
position startpos moves e2e4 e7e5 g1f3 b8c6
go depth 6
```

Supported options (subset):
- Hash (MB), Contempt, MoveOverhead
- Threads, Ponder, MultiPV, SyzygyPath/ProbeDepth, UseNNUE/EvalFile (placeholders for now)

### Perft tests
```
/workspace/phish/build/tests/phish_perft /workspace/phish/tests/perft/perft_positions.txt
```
Format:
```
<fen or startpos>;<depth>;<expected_nodes>
```

## Android / DroidFish (UCI engine)
DroidFish supports UCI engines on Android. You need an Android-native binary (AArch64/ARMv7) built with the Android NDK:

1) Install Android NDK and CMake (Android Studio or CLI).
2) Build a static binary for arm64-v8a:
- Create a toolchain build directory and use Android toolchain file:
```
cmake -S /workspace/phish -B /workspace/phish/build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release -DPHISH_ENABLE_LTO=OFF
cmake --build /workspace/phish/build-android -j
```
3) The engine binary will be at (example path):
```
/workspace/phish/build-android/phish
```
4) Copy the binary to your phone and make it executable (if needed). In DroidFish:
- Menu → Manage Chess Engines → UCI engine → Add Engine
- Select the `phish` binary

Notes:
- Some Android devices require PIE and no-glibc dependencies. The NDK cross-compile above produces a compatible binary.
- If DroidFish cannot load the engine due to permissions, use a file manager to set execute permission or place it under app-accessible storage.
- Current build has no NNUE file dependency; future versions may require copying a `.nnue` file alongside the binary.

If Android is not supported in your environment, build on-device with Termux:
- Install `clang`, `cmake`, `make` in Termux
- Build with:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPHISH_ENABLE_LTO=OFF
cmake --build build -j
```
Then add the `build/phish` binary to DroidFish as above.

## Optimization Notes
- Release builds use `-O3 -DNDEBUG`, LTO/IPO when supported
- Optional `-march=native -mtune=native` for local tuning
- Optional `-Ofast` via `-DPHISH_ENABLE_OFAST=ON`
- Leaner builds: `-fno-exceptions -fno-rtti` enabled if supported
- Linker tuned with `-Wl,-O1,--as-needed` on GCC/Clang

Future performance work:
- Magic bitboards or PEXT for sliders
- SEE, LMR/LMP, futility/razoring/IID, killer/history/countermoves
- SIMD NNUE inference (AVX2/NEON), incremental updates
- SMP (Lazy SMP), lock-free clustered TT, prefetching
- Time management, pondering, Syzygy

## Project layout
```
phish/
 ├─ engine/
 │   ├─ bitboard/
 │   ├─ board/
 │   ├─ movegen/
 │   ├─ search/
 │   ├─ uci/
 │   └─ util/
 └─ tests/
     └─ perft/
```

## Version
- 0.1.0 (experimental)

## Disclaimer
- Prototype engine; not tuned; subject to change.