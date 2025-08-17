#pragma once

#include <cstdint>

#include "engine/util/types.h"

namespace phish::bitboard {

extern U64 KNIGHT_ATTACKS[64];
extern U64 KING_ATTACKS[64];
extern U64 PAWN_ATTACKS[2][64];

extern U64 FILE_MASKS[8];
extern U64 RANK_MASKS[8];

void init();

U64 sliding_attacks_rook(Square from, U64 occ);
U64 sliding_attacks_bishop(Square from, U64 occ);

} // namespace phish::bitboard