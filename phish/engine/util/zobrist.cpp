#include "engine/util/zobrist.h"

#include <random>

namespace phish::zobrist {

U64 PIECE_SQUARE[12][64];
U64 CASTLING[16];
U64 EP_FILE[8];
U64 SIDE_TO_MOVE;

void init() {
    std::mt19937_64 rng(0x9E3779B97F4A7C15ULL);
    auto rnd = [&]() -> U64 { return rng(); };

    for (int p = 0; p < 12; ++p)
        for (int s = 0; s < 64; ++s)
            PIECE_SQUARE[p][s] = rnd();

    for (int i = 0; i < 16; ++i) CASTLING[i] = rnd();
    for (int f = 0; f < 8; ++f) EP_FILE[f] = rnd();
    SIDE_TO_MOVE = rnd();
}

} // namespace phish::zobrist