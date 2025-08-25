#include "KestoG_Core.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

#define popcount __builtin_popcountll
#define bitscan_forward __builtin_ffsll

namespace kestog_core {

    const u64 BOARD_MASK = 0xFFFFFFFF;
    const u64 COL_A = 0x11111111;
    const u64 COL_H = 0x88888888;
    const u64 NOT_COL_A = BOARD_MASK & ~COL_A;
    const u64 NOT_COL_H = BOARD_MASK & ~COL_H;
    const u64 PROMO_RANK_WHITE = 0xF0000000;
    const u64 PROMO_RANK_BLACK = 0x0000000F;

    void find_king_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, u64 opponents, u64 empty);

    // ИСПРАВЛЕННАЯ ВЕРСИЯ: Возвращена логика для всех 4 направлений
    void find_man_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, int color, u64 opponents, u64 empty) {
        bool can_jump_further = false;
        u64 promo_rank = (color == 1) ? PROMO_RANK_WHITE : PROMO_RANK_BLACK;

        // Массивы для направлений и их "охранников" (границ доски)
        int dirs[] = {4, 5, -4, -5};
        u64 guards_from[] = {NOT_COL_A, NOT_COL_H, NOT_COL_H, NOT_COL_A};
        u64 guards_jumped[] = {NOT_COL_A, NOT_COL_H, NOT_COL_H, NOT_COL_A};

        for (int i = 0; i < 4; ++i) {
            int dir = dirs[i];
            
            // Пропускаем ходы "назад" для соответствующего цвета, если это не взятие
            // В поддавках шашка бьет в любую сторону, поэтому эта проверка не нужна.
            // if ((color == 1 && dir < 0) || (color == 2 && dir > 0)) continue;

            if (current_pos & guards_from[i]) {
                u64 jumped_piece = (dir > 0) ? (current_pos << dir) : (current_pos >> -dir);
                if ((jumped_piece & guards_jumped[i]) && (jumped_piece & opponents) && !(captured & jumped_piece)) {
                    u64 land_pos = (dir > 0) ? (jumped_piece << dir) : (jumped_piece >> -dir);
                    if (land_pos & empty) {
                        can_jump_further = true;
                        if (land_pos & promo_rank) {
                            // Промоция! Продолжаем как дамка.
                            find_king_jumps(captures, start_pos, land_pos, captured | jumped_piece, opponents, empty ^ land_pos);
                        } else {
                            find_man_jumps(captures, start_pos, land_pos, captured | jumped_piece, color, opponents, empty ^ land_pos);
                        }
                    }
                }
            }
        }

        if (!can_jump_further && captured > 0) {
            captures.push_back({start_pos, current_pos, captured, (current_pos & promo_rank) != 0});
        }
    }

    // ИСПРАВЛЕННАЯ ВЕРСИЯ: Корректное обновление 'empty'
    void find_king_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, u64 opponents, u64 empty) {
        bool can_jump_further = false;
        int dirs[] = {5, 4, -5, -4};
        u64 guards[] = {NOT_COL_H, NOT_COL_A, NOT_COL_A, NOT_COL_H};

        for (int i = 0; i < 4; ++i) {
            int dir = dirs[i];
            u64 guard = guards[i];
            for (u64 path = current_pos; (path & guard); ) {
                path = (dir > 0) ? (path << dir) : (path >> -dir);
                if (!(path & empty)) {
                    if ((path & opponents) && !(captured & path)) {
                        u64 land_start = (dir > 0) ? (path << dir) : (path >> -dir);
                        if ((path & guard) && (land_start & empty)) {
                            // ИСПРАВЛЕНИЕ: Создаем новую маску пустых полей, включая побитую фигуру
                            u64 new_empty = empty | path;
                            for (u64 land_path = land_start; (land_path & guard); ) {
                                can_jump_further = true;
                                find_king_jumps(captures, start_pos, land_path, captured | path, opponents, new_empty ^ land_path);
                                land_path = (dir > 0) ? (land_path << dir) : (land_path >> -dir);
                                if (!(land_path & new_empty)) break;
                            }
                        }
                    }
                    break;
                }
            }
        }

        if (!can_jump_further && captured > 0) {
            captures.push_back({start_pos, current_pos, captured, false});
        }
    }

    // Остальные функции остаются без изменений, так как они уже корректны
    
    std::vector<Move> generate_captures(const Bitboard& board, int color_to_move) {
        std::vector<Move> captures;
        u64 my_pieces = (color_to_move == 1) ? board.white_men : board.black_men;
        u64 opponents = (color_to_move == 1) ? board.black_men : board.white_men;
        u64 empty = BOARD_MASK & ~(board.white_men | board.black_men);

        u64 men = my_pieces & ~board.kings;
        while(men) { u64 p = 1ULL << (bitscan_forward(men)-1); find_man_jumps(captures, p, p, 0, color_to_move, opponents, empty); men &= men-1; }

        u64 kings = my_pieces & board.kings;
        while(kings) { u64 p = 1ULL << (bitscan_forward(kings)-1); find_king_jumps(captures, p, p, 0, opponents, empty); kings &= kings-1; }

        return captures;
    }

    std::vector<Move> generate_quiet_moves(const Bitboard& board, int color_to_move) {
        std::vector<Move> moves;
        const u64 empty = BOARD_MASK & ~(board.white_men | board.black_men);

        if (color_to_move == 1) { // WHITE
            u64 men = board.white_men & ~board.kings;
            u64 movers_4 = ((men & NOT_COL_A) << 4) & empty;
            u64 movers_5 = ((men & NOT_COL_H) << 5) & empty;
            while(movers_4) { u64 t = 1ULL << (bitscan_forward(movers_4) - 1); moves.push_back({t >> 4, t, 0, (t & PROMO_RANK_WHITE) != 0}); movers_4 &= movers_4 - 1; }
            while(movers_5) { u64 t = 1ULL << (bitscan_forward(movers_5) - 1); moves.push_back({t >> 5, t, 0, (t & PROMO_RANK_WHITE) != 0}); movers_5 &= movers_5 - 1; }
        } else { // BLACK
            u64 men = board.black_men & ~board.kings;
            u64 movers_4 = ((men & NOT_COL_H) >> 4) & empty;
            u64 movers_5 = ((men & NOT_COL_A) >> 5) & empty;
            while(movers_4) { u64 t = 1ULL << (bitscan_forward(movers_4) - 1); moves.push_back({t << 4, t, 0, (t & PROMO_RANK_BLACK) != 0}); movers_4 &= movers_4 - 1; }
            while(movers_5) { u64 t = 1ULL << (bitscan_forward(movers_5) - 1); moves.push_back({t << 5, t, 0, (t & PROMO_RANK_BLACK) != 0}); movers_5 &= movers_5 - 1; }
        }
        
        u64 kings = ((color_to_move == 1) ? board.white_men : board.black_men) & board.kings;
        while(kings) {
            u64 p = 1ULL << (bitscan_forward(kings) - 1);
            int dirs[] = {5, 4, -5, -4};
            u64 guards[] = {NOT_COL_H, NOT_COL_A, NOT_COL_A, NOT_COL_H};
            for (int i = 0; i < 4; ++i) {
                int dir = dirs[i];
                u64 guard = guards[i];
                for (u64 path = p; (path & guard); ) {
                    path = (dir > 0) ? (path << dir) : (path >> -dir);
                    if (path & empty) {
                        moves.push_back({p, path, 0, false});
                    } else {
                        break;
                    }
                }
            }
            kings &= kings - 1;
        }
        return moves;
    }

    Bitboard apply_move(const Bitboard& b, const Move& m, int c) {
        Bitboard next_b = b; u64 from_to = m.mask_from | m.mask_to;
        if (c==1) {
            next_b.white_men ^= from_to;
            if (b.kings & m.mask_from) next_b.kings ^= from_to;
            if (m.captured_pieces) { next_b.black_men &= ~m.captured_pieces; next_b.kings &= ~m.captured_pieces; }
            if (m.becomes_king) { next_b.kings |= m.mask_to; }
        } else {
            next_b.black_men ^= from_to;
            if (b.kings & m.mask_from) next_b.kings ^= from_to;
            if (m.captured_pieces) { next_b.white_men &= ~m.captured_pieces; next_b.kings &= ~m.captured_pieces; }
            if (m.becomes_king) { next_b.kings |= m.mask_to; }
        }
        return next_b;
    }

    const int PST[32] = {
        10, 10, 10, 10,
         8,  8,  8,  8,
         6,  6,  6,  6,
         4,  4,  4,  4,
         2,  2,  2,  2,
         1,  1,  1,  1,
         0,  0,  0,  0,
         0,  0,  0,  0
    };

    int evaluate_giveaway(const Bitboard& b, int c) {
        int wm_count = popcount(b.white_men);
        int bm_count = popcount(b.black_men);

        if (wm_count == 0) return (c == 1) ? -10000 : 10000;
        if (bm_count == 0) return (c == 2) ? -10000 : 10000;

        int white_score = 0;
        int black_score = 0;

        u64 wm = b.white_men & ~b.kings;
        u64 wk = b.white_men & b.kings;
        u64 bm = b.black_men & ~b.kings;
        u64 bk = b.black_men & b.kings;

        white_score += popcount(wm) * 100 + popcount(wk) * 300;
        black_score += popcount(bm) * 100 + popcount(bk) * 300;

        for (int i = 0; i < 32; ++i) {
            u64 mask = 1ULL << i;
            if (wm & mask) white_score += PST[i];
            if (bm & mask) black_score += PST[31 - i];
        }

        int eval = (c == 1) ? (white_score - black_score) : (black_score - white_score);
        return eval;
    }
}
