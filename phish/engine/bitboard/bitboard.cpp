#include "engine/bitboard/bitboard.h"

#include <cassert>

namespace phish::bitboard {

U64 KNIGHT_ATTACKS[64];
U64 KING_ATTACKS[64];
U64 PAWN_ATTACKS[2][64];
U64 FILE_MASKS[8];
U64 RANK_MASKS[8];

static bool is_ok(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

static void set_knight_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        int f = sq & 7, r = sq >> 3;
        U64 bb = 0;
        const int df[8] = {1,2,2,1,-1,-2,-2,-1};
        const int dr[8] = {2,1,-1,-2,-2,-1,1,2};
        for (int i = 0; i < 8; ++i) {
            int nf = f + df[i], nr = r + dr[i];
            if (is_ok(nf, nr)) bb |= Bit(make_square(nf, nr));
        }
        KNIGHT_ATTACKS[sq] = bb;
    }
}

static void set_king_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        int f = sq & 7, r = sq >> 3;
        U64 bb = 0;
        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) continue;
                int nf = f + df, nr = r + dr;
                if (is_ok(nf, nr)) bb |= Bit(make_square(nf, nr));
            }
        }
        KING_ATTACKS[sq] = bb;
    }
}

static void set_pawn_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        int f = sq & 7, r = sq >> 3;
        U64 w = 0, b = 0;
        if (is_ok(f - 1, r + 1)) w |= Bit(make_square(f - 1, r + 1));
        if (is_ok(f + 1, r + 1)) w |= Bit(make_square(f + 1, r + 1));
        if (is_ok(f - 1, r - 1)) b |= Bit(make_square(f - 1, r - 1));
        if (is_ok(f + 1, r - 1)) b |= Bit(make_square(f + 1, r - 1));
        PAWN_ATTACKS[WHITE][sq] = w;
        PAWN_ATTACKS[BLACK][sq] = b;
    }
}

static void set_file_rank_masks() {
    for (int f = 0; f < 8; ++f) {
        U64 m = 0;
        for (int r = 0; r < 8; ++r) m |= Bit(make_square(f, r));
        FILE_MASKS[f] = m;
    }
    for (int r = 0; r < 8; ++r) {
        U64 m = 0;
        for (int f = 0; f < 8; ++f) m |= Bit(make_square(f, r));
        RANK_MASKS[r] = m;
    }
}

void init() {
    set_knight_attacks();
    set_king_attacks();
    set_pawn_attacks();
    set_file_rank_masks();
}

U64 sliding_attacks_rook(Square from, U64 occ) {
    U64 attacks = 0;
    int f = file_of(from), r = rank_of(from);
    // North
    for (int nr = r + 1; nr < 8; ++nr) {
        Square s = make_square(f, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    // South
    for (int nr = r - 1; nr >= 0; --nr) {
        Square s = make_square(f, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    // East
    for (int nf = f + 1; nf < 8; ++nf) {
        Square s = make_square(nf, r);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    // West
    for (int nf = f - 1; nf >= 0; --nf) {
        Square s = make_square(nf, r);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    return attacks;
}

U64 sliding_attacks_bishop(Square from, U64 occ) {
    U64 attacks = 0;
    int f = file_of(from), r = rank_of(from);
    // NE
    for (int nf = f + 1, nr = r + 1; nf < 8 && nr < 8; ++nf, ++nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    // NW
    for (int nf = f - 1, nr = r + 1; nf >= 0 && nr < 8; --nf, ++nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    // SE
    for (int nf = f + 1, nr = r - 1; nf < 8 && nr >= 0; ++nf, --nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    // SW
    for (int nf = f - 1, nr = r - 1; nf >= 0 && nr >= 0; --nf, --nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    return attacks;
}

} // namespace phish::bitboard