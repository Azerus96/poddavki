#include "KestoG_Core.hpp"
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <cstring> // Для memset

#define popcount __builtin_popcountll
#define bitscan_forward __builtin_ffsll

namespace kestog_core {

    // --- Глобальные переменные движка ---
    constexpr int MAX_PLY = 64;
    constexpr int MATE_SCORE = 10000;
    constexpr int INFINITY_SCORE = 10001;

    // Zobrist Hashing
    u64 ZOBRIST[32][4]; // [square][piece_type: wm, bm, wk, bk]
    u64 ZOBRIST_BLACK_TO_MOVE;

    // Транспозиционная таблица
    std::vector<TT_Entry> transposition_table;
    u64 tt_mask;

    // Эвристики упорядочивания ходов
    Move killer_moves[MAX_PLY][2];
    int history[32][32];

    // Статистика
    long long nodes_searched;
    std::chrono::steady_clock::time_point search_start_time;
    int time_limit_ms;
    bool stop_search_flag;

    // --- Константы доски ---
    const u64 BOARD_MASK = 0xFFFFFFFF;

    const u64 COL_A = (1ULL << 4) | (1ULL << 12) | (1ULL << 20) | (1ULL << 28);
    const u64 COL_B = (1ULL << 0) | (1ULL << 8)  | (1ULL << 16) | (1ULL << 24);
    const u64 COL_G = (1ULL << 7) | (1ULL << 15) | (1ULL << 23) | (1ULL << 31);
    const u64 COL_H = (1ULL << 3) | (1ULL << 11) | (1ULL << 19) | (1ULL << 27);

    const u64 NOT_A_COL = ~COL_A;
    const u64 NOT_H_COL = ~COL_H;
    const u64 NOT_A_B_COL = ~(COL_A | COL_B);
    const u64 NOT_G_H_COL = ~(COL_G | COL_H);
    
    const u64 PROMO_RANK_WHITE = (1ULL << 28) | (1ULL << 29) | (1ULL << 30) | (1ULL << 31);
    const u64 PROMO_RANK_BLACK = (1ULL << 0) | (1ULL << 1) | (1ULL << 2) | (1ULL << 3);

    // --- Прототипы внутренних функций ---
    void find_king_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, u64 opponents, u64 empty);
    void find_man_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, int color, u64 opponents, u64 empty);
    std::vector<Move> generate_captures(const Bitboard& board, int color_to_move);
    std::vector<Move> generate_quiet_moves(const Bitboard& board, int color_to_move);
    int evaluate_giveaway(const Bitboard& b);
    int quiescence_search(Bitboard& board, int alpha, int beta, int color, int ply);
    int negamax(Bitboard& board, int alpha, int beta, int depth, int color, int ply);

    // --- Инициализация ---
    void init_engine(int tt_size_mb) {
        std::mt19937_64 rng(0xdeadbeef);
        for (int i = 0; i < 32; ++i) {
            for (int j = 0; j < 4; ++j) {
                ZOBRIST[i][j] = rng();
            }
        }
        ZOBRIST_BLACK_TO_MOVE = rng();

        size_t tt_size = (size_t)tt_size_mb * 1024 * 1024 / sizeof(TT_Entry);
        size_t power_of_2_size = 1;
        while (power_of_2_size * 2 <= tt_size && power_of_2_size != 0) {
            power_of_2_size *= 2;
        }
        transposition_table.assign(power_of_2_size, TT_Entry());
        tt_mask = power_of_2_size - 1;
        std::cout << "TT initialized with " << power_of_2_size << " entries (" << tt_size_mb << "MB)." << std::endl;
    }

    u64 calculate_hash(const Bitboard& board, int color_to_move) {
        u64 hash = 0;
        u64 wm = board.white_men & ~board.kings;
        u64 bm = board.black_men & ~board.kings;
        u64 wk = board.white_men & board.kings;
        u64 bk = board.black_men & board.kings;

        for (int i = 0; i < 32; ++i) {
            u64 mask = 1ULL << i;
            if (wm & mask) hash ^= ZOBRIST[i][0];
            if (bm & mask) hash ^= ZOBRIST[i][1];
            if (wk & mask) hash ^= ZOBRIST[i][2];
            if (bk & mask) hash ^= ZOBRIST[i][3];
        }
        if (color_to_move == 2) {
            hash ^= ZOBRIST_BLACK_TO_MOVE;
        }
        return hash;
    }

    // --- Генерация ходов ---

    void find_king_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, u64 opponents, u64 empty) {
        bool can_jump_further = false;
        int dirs[] = {9, 7, -7, -9}; // СВ, СЗ, ЮВ, ЮЗ
        u64 guards[] = {NOT_G_H_COL, NOT_A_B_COL, NOT_G_H_COL, NOT_A_B_COL};

        for (int i = 0; i < 4; ++i) {
            int dir = dirs[i];
            u64 guard = guards[i];
            
            u64 path = current_pos;
            while ((path = (dir > 0) ? (path << dir) : (path >> -dir)) & empty & guard);

            if ((path & opponents) && !(captured & path) && (path & guard)) {
                u64 jumped_pos = path;
                u64 land_pos_check = (dir > 0) ? (jumped_pos << dir) : (jumped_pos >> -dir);

                if ((land_pos_check & empty) && (land_pos_check & guard)) {
                    for (u64 land_path = land_pos_check; (land_path & guard); land_path = (dir > 0) ? (land_path << dir) : (land_path >> -dir)) {
                        if (!(land_path & empty)) break;
                        
                        can_jump_further = true;
                        u64 new_captured = captured | jumped_pos;
                        u64 new_opponents = opponents & ~jumped_pos;
                        u64 new_empty = (empty | current_pos | jumped_pos) & ~land_path;
                        
                        find_king_jumps(captures, start_pos, land_path, new_captured, new_opponents, new_empty);
                    }
                }
            }
        }

        if (!can_jump_further && captured > 0) {
            captures.push_back({start_pos, current_pos, captured, false, 0});
        }
    }

    void find_man_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, int color, u64 opponents, u64 empty) {
        bool can_jump_further = false;
        u64 promo_rank = (color == 1) ? PROMO_RANK_WHITE : PROMO_RANK_BLACK;
        
        int from_idx = bitscan_forward(current_pos) - 1;
        int row = from_idx / 4;

        // СВ
        if (!(current_pos & COL_G) && !(current_pos & COL_H)) {
            int jumped_idx = from_idx + (row % 2 == 0 ? 5 : 4);
            int land_idx = from_idx + 9;
            if (land_idx < 32) {
                u64 jumped_pos = 1ULL << jumped_idx;
                u64 land_pos = 1ULL << land_idx;
                if ((jumped_pos & opponents) && !(captured & jumped_pos) && (land_pos & empty)) {
                    can_jump_further = true;
                    u64 new_captured = captured | jumped_pos;
                    u64 new_opponents = opponents & ~jumped_pos;
                    u64 new_empty = (empty | current_pos | jumped_pos) & ~land_pos;
                    if (land_pos & promo_rank) find_king_jumps(captures, start_pos, land_pos, new_captured, new_opponents, new_empty);
                    else find_man_jumps(captures, start_pos, land_pos, new_captured, color, new_opponents, new_empty);
                }
            }
        }
        // СЗ
        if (!(current_pos & COL_A) && !(current_pos & COL_B)) {
            int jumped_idx = from_idx + (row % 2 == 0 ? 4 : 3);
            int land_idx = from_idx + 7;
            if (land_idx < 32) {
                u64 jumped_pos = 1ULL << jumped_idx;
                u64 land_pos = 1ULL << land_idx;
                if ((jumped_pos & opponents) && !(captured & jumped_pos) && (land_pos & empty)) {
                    can_jump_further = true;
                    u64 new_captured = captured | jumped_pos;
                    u64 new_opponents = opponents & ~jumped_pos;
                    u64 new_empty = (empty | current_pos | jumped_pos) & ~land_pos;
                    if (land_pos & promo_rank) find_king_jumps(captures, start_pos, land_pos, new_captured, new_opponents, new_empty);
                    else find_man_jumps(captures, start_pos, land_pos, new_captured, color, new_opponents, new_empty);
                }
            }
        }
        // ЮЗ
        if (!(current_pos & COL_A) && !(current_pos & COL_B)) {
            int jumped_idx = from_idx - (row % 2 == 0 ? 4 : 5);
            int land_idx = from_idx - 9;
            if (land_idx >= 0) {
                u64 jumped_pos = 1ULL << jumped_idx;
                u64 land_pos = 1ULL << land_idx;
                if ((jumped_pos & opponents) && !(captured & jumped_pos) && (land_pos & empty)) {
                    can_jump_further = true;
                    u64 new_captured = captured | jumped_pos;
                    u64 new_opponents = opponents & ~jumped_pos;
                    u64 new_empty = (empty | current_pos | jumped_pos) & ~land_pos;
                    if (land_pos & promo_rank) find_king_jumps(captures, start_pos, land_pos, new_captured, new_opponents, new_empty);
                    else find_man_jumps(captures, start_pos, land_pos, new_captured, color, new_opponents, new_empty);
                }
            }
        }
        // ЮВ
        if (!(current_pos & COL_G) && !(current_pos & COL_H)) {
            int jumped_idx = from_idx - (row % 2 == 0 ? 3 : 4);
            int land_idx = from_idx - 7;
            if (land_idx >= 0) {
                u64 jumped_pos = 1ULL << jumped_idx;
                u64 land_pos = 1ULL << land_idx;
                if ((jumped_pos & opponents) && !(captured & jumped_pos) && (land_pos & empty)) {
                    can_jump_further = true;
                    u64 new_captured = captured | jumped_pos;
                    u64 new_opponents = opponents & ~jumped_pos;
                    u64 new_empty = (empty | current_pos | jumped_pos) & ~land_pos;
                    if (land_pos & promo_rank) find_king_jumps(captures, start_pos, land_pos, new_captured, new_opponents, new_empty);
                    else find_man_jumps(captures, start_pos, land_pos, new_captured, color, new_opponents, new_empty);
                }
            }
        }

        if (!can_jump_further && captured > 0) {
            bool becomes_king = (current_pos & promo_rank) != 0;
            captures.push_back({start_pos, current_pos, captured, becomes_king, 0});
        }
    }

    std::vector<Move> generate_captures(const Bitboard& board, int color_to_move) {
        std::vector<Move> captures;
        u64 my_pieces = (color_to_move == 1) ? board.white_men : board.black_men;
        u64 opponents = (color_to_move == 1) ? board.black_men : board.white_men;
        u64 empty = BOARD_MASK & ~(board.white_men | board.black_men);
        
        u64 men = my_pieces & ~board.kings;
        while(men) { 
            u64 p = 1ULL << (bitscan_forward(men)-1); 
            find_man_jumps(captures, p, p, 0, color_to_move, opponents, empty); 
            men &= men-1; 
        }
        
        u64 kings = my_pieces & board.kings;
        while(kings) { 
            u64 p = 1ULL << (bitscan_forward(kings)-1); 
            find_king_jumps(captures, p, p, 0, opponents, empty); 
            kings &= kings-1; 
        }
        return captures;
    }

    // =================================================================================
    // >>>>> ФИНАЛЬНОЕ ИСПРАВЛЕНИЕ: Добавлена логика "тихого хода с прыжком" <<<<<
    // =================================================================================
    std::vector<Move> generate_quiet_moves(const Bitboard& board, int color_to_move) {
        std::vector<Move> moves;
        const u64 empty = BOARD_MASK & ~(board.white_men | board.black_men);
        u64 my_pieces = (color_to_move == 1) ? board.white_men : board.black_men;
        u64 my_men = my_pieces & ~board.kings;

        // 1. Обычные тихие ходы
        u64 temp_men = my_men;
        while (temp_men) {
            u64 p = 1ULL << (bitscan_forward(temp_men) - 1);
            int from_idx = bitscan_forward(p) - 1;
            int row = from_idx / 4;

            if (color_to_move == 1) { // Белые
                if (row % 2 == 0) { // Ряды 1,3,5,7
                    if (!(p & COL_A)) { u64 t = p << 4; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_WHITE) != 0, 0}); }
                    if (!(p & COL_H)) { u64 t = p << 5; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_WHITE) != 0, 0}); }
                } else { // Ряды 2,4,6,8
                    if (!(p & COL_A)) { u64 t = p << 3; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_WHITE) != 0, 0}); }
                    if (!(p & COL_H)) { u64 t = p << 4; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_WHITE) != 0, 0}); }
                }
            } else { // Черные
                if (row % 2 == 0) { // Ряды 1,3,5,7
                    if (!(p & COL_A)) { u64 t = p >> 5; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_BLACK) != 0, 0}); }
                    if (!(p & COL_H)) { u64 t = p >> 4; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_BLACK) != 0, 0}); }
                } else { // Ряды 2,4,6,8
                    if (!(p & COL_A)) { u64 t = p >> 4; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_BLACK) != 0, 0}); }
                    if (!(p & COL_H)) { u64 t = p >> 3; if (t & empty) moves.push_back({p, t, 0, (t & PROMO_RANK_BLACK) != 0, 0}); }
                }
            }
            temp_men &= temp_men - 1;
        }

        // 2. Тихие ходы с прыжком через свои шашки
        temp_men = my_men;
        while (temp_men) {
            u64 p = 1ULL << (bitscan_forward(temp_men) - 1);
            int from_idx = bitscan_forward(p) - 1;
            int row = from_idx / 4;

            // Проверяем все 4 направления для прыжка
            // СВ
            if (!(p & COL_G) && !(p & COL_H)) {
                int jumped_idx = from_idx + (row % 2 == 0 ? 5 : 4);
                int land_idx = from_idx + 9;
                if (land_idx < 32) {
                    u64 jumped_pos = 1ULL << jumped_idx;
                    u64 land_pos = 1ULL << land_idx;
                    if ((jumped_pos & my_pieces) && (land_pos & empty)) {
                        moves.push_back({p, land_pos, 0, (land_pos & PROMO_RANK_WHITE) != 0, 0});
                    }
                }
            }
            // СЗ
            if (!(p & COL_A) && !(p & COL_B)) {
                int jumped_idx = from_idx + (row % 2 == 0 ? 4 : 3);
                int land_idx = from_idx + 7;
                if (land_idx < 32) {
                    u64 jumped_pos = 1ULL << jumped_idx;
                    u64 land_pos = 1ULL << land_idx;
                    if ((jumped_pos & my_pieces) && (land_pos & empty)) {
                        moves.push_back({p, land_pos, 0, (land_pos & PROMO_RANK_WHITE) != 0, 0});
                    }
                }
            }
            // ЮЗ
            if (!(p & COL_A) && !(p & COL_B)) {
                int jumped_idx = from_idx - (row % 2 == 0 ? 4 : 5);
                int land_idx = from_idx - 9;
                if (land_idx >= 0) {
                    u64 jumped_pos = 1ULL << jumped_idx;
                    u64 land_pos = 1ULL << land_idx;
                    if ((jumped_pos & my_pieces) && (land_pos & empty)) {
                        moves.push_back({p, land_pos, 0, (land_pos & PROMO_RANK_BLACK) != 0, 0});
                    }
                }
            }
            // ЮВ
            if (!(p & COL_G) && !(p & COL_H)) {
                int jumped_idx = from_idx - (row % 2 == 0 ? 3 : 4);
                int land_idx = from_idx - 7;
                if (land_idx >= 0) {
                    u64 jumped_pos = 1ULL << jumped_idx;
                    u64 land_pos = 1ULL << land_idx;
                    if ((jumped_pos & my_pieces) && (land_pos & empty)) {
                        moves.push_back({p, land_pos, 0, (land_pos & PROMO_RANK_BLACK) != 0, 0});
                    }
                }
            }
            temp_men &= temp_men - 1;
        }

        // 3. Ходы дамок
        u64 kings = my_pieces & ~my_men;
        while(kings) {
            u64 p = 1ULL << (bitscan_forward(kings) - 1);
            int dirs[] = {5, 4, -5, -4};
            u64 guards[] = {NOT_H_COL, NOT_A_COL, NOT_A_COL, NOT_H_COL};
            for (int i = 0; i < 4; ++i) {
                int dir = dirs[i];
                u64 guard = guards[i];
                for (u64 path = p; (path & guard); ) {
                    path = (dir > 0) ? (path << dir) : (path >> -dir);
                    if (path & empty) { moves.push_back({p, path, 0, false, 0}); } else { break; }
                }
            }
            kings &= kings - 1;
        }
        return moves;
    }

    std::vector<Move> generate_legal_moves(const Bitboard& board, int color_to_move) {
        auto captures = generate_captures(board, color_to_move);
        if (!captures.empty()) {
            int max_captured = 0;
            for (const auto& m : captures) {
                max_captured = std::max(max_captured, (int)popcount(m.captured_pieces));
            }
            std::vector<Move> max_captures;
            for (const auto& m : captures) {
                if (popcount(m.captured_pieces) == max_captured) {
                    max_captures.push_back(m);
                }
            }
            return max_captures;
        }
        return generate_quiet_moves(board, color_to_move);
    }

    // --- Остальной код без изменений ---
    const int PST[32] = { 10,10,10,10, 8,8,8,8, 6,6,6,6, 4,4,4,4, 2,2,2,2, 1,1,1,1, 0,0,0,0, 0,0,0,0 };
    int evaluate_giveaway(const Bitboard& b) {
        int white_material = popcount(b.white_men & ~b.kings) * 100 + popcount(b.white_men & b.kings) * 300;
        int black_material = popcount(b.black_men & ~b.kings) * 100 + popcount(b.black_men & b.kings) * 300;
        int white_pos = 0;
        int black_pos = 0;
        u64 wm = b.white_men & ~b.kings;
        u64 bm = b.black_men & ~b.kings;
        for (int i = 0; i < 32; ++i) {
            u64 mask = 1ULL << i;
            if (wm & mask) white_pos += PST[i];
            if (bm & mask) black_pos += PST[31 - i];
        }
        return (black_material - white_material) + (black_pos - white_pos);
    }
    Bitboard apply_move(const Bitboard& b, const Move& m, int c) {
        Bitboard next_b = b;
        u64 from_to = m.mask_from | m.mask_to;
        bool is_king_before_move = (b.kings & m.mask_from) != 0;
        if (c == 1) {
            next_b.white_men ^= from_to;
            if (m.captured_pieces) next_b.black_men &= ~m.captured_pieces;
        } else {
            next_b.black_men ^= from_to;
            if (m.captured_pieces) next_b.white_men &= ~m.captured_pieces;
        }
        if (is_king_before_move) {
            next_b.kings ^= from_to;
        } else if (m.becomes_king) {
            next_b.kings |= m.mask_to;
        }
        if (m.captured_pieces) {
            next_b.kings &= ~m.captured_pieces;
        }
        next_b.hash = calculate_hash(next_b, 3 - c);
        return next_b;
    }
    void score_moves(std::vector<Move>& moves, const Move& tt_move, int ply) {
        for (auto& move : moves) {
            if (move.mask_from == tt_move.mask_from && move.mask_to == tt_move.mask_to) {
                move.score = 100000;
            } else if (move.captured_pieces > 0) {
                move.score = 90000 + popcount(move.captured_pieces);
            } else if ((move.mask_from == killer_moves[ply][0].mask_from && move.mask_to == killer_moves[ply][0].mask_to) ||
                       (move.mask_from == killer_moves[ply][1].mask_from && move.mask_to == killer_moves[ply][1].mask_to)) {
                move.score = 80000;
            } else {
                move.score = history[bitscan_forward(move.mask_from)-1][bitscan_forward(move.mask_to)-1];
            }
        }
        std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
            return a.score > b.score;
        });
    }
    int negamax(Bitboard& board, int alpha, int beta, int depth, int color, int ply) {
        nodes_searched++;
        if ((nodes_searched & 2047) == 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start_time).count() > time_limit_ms) {
                stop_search_flag = true;
            }
        }
        if (stop_search_flag || ply >= MAX_PLY) return 0;
        TT_Entry& tt_entry = transposition_table[board.hash & tt_mask];
        if (tt_entry.hash_lock == board.hash && tt_entry.depth >= depth) {
            if (tt_entry.flag == TT_EXACT) return tt_entry.score;
            if (tt_entry.flag == TT_ALPHA && tt_entry.score <= alpha) return alpha;
            if (tt_entry.flag == TT_BETA && tt_entry.score >= beta) return beta;
        }
        if (depth <= 0) {
            return quiescence_search(board, alpha, beta, color, 0);
        }
        auto moves = generate_legal_moves(board, color);
        if (moves.empty()) {
            return MATE_SCORE - ply;
        }
        score_moves(moves, tt_entry.best_move, ply);
        int best_score = -INFINITY_SCORE;
        Move best_move = moves[0];
        TT_FLAG flag = TT_ALPHA;
        for (const auto& move : moves) {
            Bitboard next_board = apply_move(board, move, color);
            if ((color == 1 && next_board.white_men == 0) || (color == 2 && next_board.black_men == 0)) {
                return MATE_SCORE - ply;
            }
            int score = -negamax(next_board, -beta, -alpha, depth - 1, 3 - color, ply + 1);
            if (stop_search_flag) return 0;
            if (score > best_score) {
                best_score = score;
                if (score > alpha) {
                    alpha = score;
                    flag = TT_EXACT;
                    best_move = move;
                    if (score >= beta) {
                        if (move.captured_pieces == 0) {
                            killer_moves[ply][1] = killer_moves[ply][0];
                            killer_moves[ply][0] = move;
                            history[bitscan_forward(move.mask_from)-1][bitscan_forward(move.mask_to)-1] += depth * depth;
                        }
                        tt_entry = {board.hash, best_score, depth, TT_BETA, best_move};
                        return beta;
                    }
                }
            }
        }
        tt_entry = {board.hash, best_score, depth, flag, best_move};
        return best_score;
    }
    int quiescence_search(Bitboard& board, int alpha, int beta, int color, int ply) {
        nodes_searched++;
        int stand_pat = (color == 1) ? evaluate_giveaway(board) : -evaluate_giveaway(board);
        if (stand_pat >= beta) return beta;
        if (alpha < stand_pat) alpha = stand_pat;
        auto captures = generate_captures(board, color);
        if (captures.empty() || ply > 8) {
            return stand_pat;
        }
        int max_captured = 0;
        for (const auto& m : captures) max_captured = std::max(max_captured, (int)popcount(m.captured_pieces));
        for (const auto& capture : captures) {
            if (popcount(capture.captured_pieces) < max_captured) continue;
            Bitboard next_board = apply_move(board, capture, color);
            int score = -quiescence_search(next_board, -beta, -alpha, 3 - color, ply + 1);
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }
    SearchResult find_best_move(const Bitboard& board, int color_to_move, int max_depth, int time_limit_ms_param) {
        nodes_searched = 0;
        stop_search_flag = false;
        time_limit_ms = time_limit_ms_param;
        search_start_time = std::chrono::steady_clock::now();
        memset(killer_moves, 0, sizeof(killer_moves));
        memset(history, 0, sizeof(history));
        Bitboard root_board = board;
        root_board.hash = calculate_hash(board, color_to_move);
        Move best_move_overall{};
        int best_score_overall = 0;
        int final_depth = 0;
        for (int current_depth = 1; current_depth <= max_depth; ++current_depth) {
            final_depth = current_depth;
            int score = negamax(root_board, -INFINITY_SCORE, INFINITY_SCORE, current_depth, color_to_move, 0);
            if (stop_search_flag && current_depth > 1) {
                final_depth = current_depth - 1;
                break;
            }
            TT_Entry& tt_entry = transposition_table[root_board.hash & tt_mask];
            best_move_overall = tt_entry.best_move;
            best_score_overall = score;
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - search_start_time).count();
            std::cout << "info depth " << current_depth << " score cp " << best_score_overall
                      << " nodes " << nodes_searched << " time " << (int)elapsed << " pv " << std::endl;
            if (abs(best_score_overall) >= MATE_SCORE - MAX_PLY) {
                break;
            }
        }
        auto end_time = std::chrono::steady_clock::now();
        double total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - search_start_time).count();
        return {best_move_overall, best_score_overall, nodes_searched, total_time, final_depth};
    }
}
