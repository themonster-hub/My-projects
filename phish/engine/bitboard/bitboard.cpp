#include "engine/bitboard/bitboard.h"

#include <cassert>
#ifdef __x86_64__
#include <immintrin.h>
#endif

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

static U64 sliding_attacks_rook_fallback(Square from, U64 occ) {
    U64 attacks = 0;
    int f = file_of(from), r = rank_of(from);
    for (int nr = r + 1; nr < 8; ++nr) {
        Square s = make_square(f, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    for (int nr = r - 1; nr >= 0; --nr) {
        Square s = make_square(f, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    for (int nf = f + 1; nf < 8; ++nf) {
        Square s = make_square(nf, r);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    for (int nf = f - 1; nf >= 0; --nf) {
        Square s = make_square(nf, r);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    return attacks;
}

static U64 sliding_attacks_bishop_fallback(Square from, U64 occ) {
    U64 attacks = 0;
    int f = file_of(from), r = rank_of(from);
    for (int nf = f + 1, nr = r + 1; nf < 8 && nr < 8; ++nf, ++nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    for (int nf = f - 1, nr = r + 1; nf >= 0 && nr < 8; --nf, ++nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    for (int nf = f + 1, nr = r - 1; nf < 8 && nr >= 0; ++nf, --nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    for (int nf = f - 1, nr = r - 1; nf >= 0 && nr >= 0; --nf, --nr) {
        Square s = make_square(nf, nr);
        attacks |= Bit(s);
        if (occ & Bit(s)) break;
    }
    return attacks;
}

#ifdef __BMI2__
// PEXT-based sliding attack generation tables
static U64 ROOK_MASK[64];
static U64 BISHOP_MASK[64];
static U64* ROOK_ATTACKS_TABLE[64];
static U64* BISHOP_ATTACKS_TABLE[64];

static U64 gen_rook_mask(Square s) {
    U64 m = 0;
    int f = file_of(s), r = rank_of(s);
    for (int nr = r + 1; nr <= 6; ++nr) m |= Bit(make_square(f, nr));
    for (int nr = r - 1; nr >= 1; --nr) m |= Bit(make_square(f, nr));
    for (int nf = f + 1; nf <= 6; ++nf) m |= Bit(make_square(nf, r));
    for (int nf = f - 1; nf >= 1; --nf) m |= Bit(make_square(nf, r));
    return m;
}

static U64 gen_bishop_mask(Square s) {
    U64 m = 0;
    int f = file_of(s), r = rank_of(s);
    for (int nf = f + 1, nr = r + 1; nf <= 6 && nr <= 6; ++nf, ++nr) m |= Bit(make_square(nf, nr));
    for (int nf = f - 1, nr = r + 1; nf >= 1 && nr <= 6; --nf, ++nr) m |= Bit(make_square(nf, nr));
    for (int nf = f + 1, nr = r - 1; nf <= 6 && nr >= 1; ++nf, --nr) m |= Bit(make_square(nf, nr));
    for (int nf = f - 1, nr = r - 1; nf >= 1 && nr >= 1; --nf, --nr) m |= Bit(make_square(nf, nr));
    return m;
}

static void init_pext_tables() {
    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        ROOK_MASK[sq] = gen_rook_mask(s);
        BISHOP_MASK[sq] = gen_bishop_mask(s);
        int rbits = __builtin_popcountll(ROOK_MASK[sq]);
        int bbits = __builtin_popcountll(BISHOP_MASK[sq]);
        ROOK_ATTACKS_TABLE[sq] = new U64[1ULL << rbits]{};
        BISHOP_ATTACKS_TABLE[sq] = new U64[1ULL << bbits]{};
        // Fill tables by iterating all subsets
        // Rook directions
        for (U64 idx = 0; idx < (1ULL << rbits); ++idx) {
            U64 occ = _pdep_u64(idx, ROOK_MASK[sq]);
            ROOK_ATTACKS_TABLE[sq][idx] = sliding_attacks_rook_fallback(s, occ);
        }
        for (U64 idx = 0; idx < (1ULL << bbits); ++idx) {
            U64 occ = _pdep_u64(idx, BISHOP_MASK[sq]);
            BISHOP_ATTACKS_TABLE[sq][idx] = sliding_attacks_bishop_fallback(s, occ);
        }
    }
}

static inline U64 rook_attacks_pext(Square s, U64 occ) {
    U64 masked = occ & ROOK_MASK[s];
    U64 idx = _pext_u64(masked, ROOK_MASK[s]);
    return ROOK_ATTACKS_TABLE[s][idx];
}

static inline U64 bishop_attacks_pext(Square s, U64 occ) {
    U64 masked = occ & BISHOP_MASK[s];
    U64 idx = _pext_u64(masked, BISHOP_MASK[s]);
    return BISHOP_ATTACKS_TABLE[s][idx];
}
#endif

void init() {
    set_knight_attacks();
    set_king_attacks();
    set_pawn_attacks();
    set_file_rank_masks();
#ifdef __BMI2__
    init_pext_tables();
#endif
}

U64 rook_attacks(Square from, U64 occ) {
#ifdef __BMI2__
    return rook_attacks_pext(from, occ);
#else
    return sliding_attacks_rook_fallback(from, occ);
#endif
}

U64 bishop_attacks(Square from, U64 occ) {
#ifdef __BMI2__
    return bishop_attacks_pext(from, occ);
#else
    return sliding_attacks_bishop_fallback(from, occ);
#endif
}

} // namespace phish::bitboard