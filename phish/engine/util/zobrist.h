#pragma once

#include <cstdint>

#include "engine/util/types.h"

namespace phish::zobrist {

extern U64 PIECE_SQUARE[12][64];
extern U64 CASTLING[16];
extern U64 EP_FILE[8];
extern U64 SIDE_TO_MOVE;

void init();

} // namespace phish::zobrist