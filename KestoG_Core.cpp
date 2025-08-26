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
    const u64 COL_A = 0x11111111;
    const u64 COL_H = 0x88888888;
    const u64 NOT_COL_A = BOARD_MASK & ~COL_A;
    const u64 NOT_COL_H = BOARD_MASK & ~COL_H;
    const u64 PROMO_RANK_WHITE = 0xF0000000;
    const u64 PROMO_RANK_BLACK = 0x0000000F;

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

    // =================================================================================
    // НАЧАЛО ИСПРАВЛЕННОГО БЛОКА: Полностью переписанная логика генерации взятий
    // =================================================================================
    void find_man_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, int color, u64 opponents, u64 empty) {
        bool can_jump_further = false;
        u64 promo_rank = (color == 1) ? PROMO_RANK_WHITE : PROMO_RANK_BLACK;
        u64 all_pieces = ~empty;

        // Направления для белых: +4, +5 (вперед), -4, -5 (назад)
        // Направления для черных: -4, -5 (вперед), +4, +5 (назад)
        int dirs[] = {4, 5, -4, -5};
        u64 guards1[] = {NOT_COL_A, NOT_COL_H, NOT_COL_H, NOT_COL_A}; // Ограничения для поля, С которого прыгаем
        u64 guards2[] = {NOT_COL_H, NOT_COL_A, NOT_COL_A, NOT_COL_H}; // Ограничения для поля, НА которое прыгаем

        for (int i = 0; i < 4; ++i) {
            int dir = dirs[i];
            // Шашки могут бить назад, но не могут ходить тихо назад
            // if (color == 1 && dir < 0) continue; // Белые не бьют назад (по международным правилам)
            // if (color == 2 && dir > 0) continue; // Черные не бьют назад (по международным правилам)
            // В русских шашках и поддавках бить назад можно, поэтому строки выше закомментированы.

            if ((current_pos & guards1[i])) {
                u64 jumped_pos = (dir > 0) ? (current_pos << dir) : (current_pos >> -dir);
                u64 land_pos = (dir > 0) ? (jumped_pos << dir) : (jumped_pos >> -dir);

                if ((jumped_pos & opponents) && !(captured & jumped_pos) && (land_pos & empty) && (land_pos & guards2[i])) {
                    can_jump_further = true;
                    u64 new_captured = captured | jumped_pos;
                    u64 new_empty = (empty & ~land_pos) | current_pos;

                    if ((land_pos & promo_rank) && !(start_pos & promo_rank)) {
                        find_king_jumps(captures, start_pos, land_pos, new_captured, opponents, new_empty);
                    } else {
                        find_man_jumps(captures, start_pos, land_pos, new_captured, color, opponents, new_empty);
                    }
                }
            }
        }

        if (!can_jump_further && captured > 0) {
            captures.push_back({start_pos, current_pos, captured, (current_pos & promo_rank) != 0 && !(start_pos & promo_rank), 0});
        }
    }

    void find_king_jumps(std::vector<Move>& captures, u64 start_pos, u64 current_pos, u64 captured, u64 opponents, u64 empty) {
        bool can_jump_further = false;
        int dirs[] = {5, 4, -5, -4};
        u64 guards[] = {NOT_COL_H, NOT_COL_A, NOT_COL_A, NOT_COL_H};

        for (int i = 0; i < 4; ++i) {
            int dir = dirs[i];
            u64 guard = guards[i];
            
            u64 path = current_pos;
            // Скользим по пустым клеткам
            while ((path = (dir > 0) ? (path << dir) : (path >> -dir)) & empty & guard);

            // Нашли препятствие. Проверяем, можем ли мы его срубить.
            if ((path & opponents) && !(captured & path) && (path & guard)) {
                u64 jumped_pos = path;
                u64 land_pos = (dir > 0) ? (jumped_pos << dir) : (jumped_pos >> -dir);

                // Если за срубленной фигурой есть хотя бы одна пустая клетка
                if ((land_pos & empty) && (land_pos & guard)) {
                    // Можем приземлиться на любую пустую клетку на этой диагонали
                    for (u64 land_path = land_pos; (land_path & guard); land_path = (dir > 0) ? (land_path << dir) : (land_path >> -dir)) {
                        if (!(land_path & empty)) break;
                        
                        can_jump_further = true;
                        u64 new_captured = captured | jumped_pos;
                        u64 new_empty = (empty & ~land_path) | current_pos | jumped_pos;
                        find_king_jumps(captures, start_pos, land_path, new_captured, opponents, new_empty);
                    }
                }
            }
        }

        if (!can_jump_further && captured > 0) {
            captures.push_back({start_pos, current_pos, captured, false, 0});
        }
    }
    // =================================================================================
    // КОНЕЦ ИСПРАВЛЕННОГО БЛОКА
    // =================================================================================


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

    std::vector<Move> generate_quiet_moves(const Bitboard& board, int color_to_move) {
        std::vector<Move> moves;
        const u64 empty = BOARD_MASK & ~(board.white_men | board.black_men);
        if (color_to_move == 1) {
            u64 men = board.white_men & ~board.kings;
            u64 movers_4 = ((men & NOT_COL_A) << 4) & empty;
            u64 movers_5 = ((men & NOT_COL_H) << 5) & empty;
            while(movers_4) { u64 t = 1ULL << (bitscan_forward(movers_4) - 1); moves.push_back({t >> 4, t, 0, (t & PROMO_RANK_WHITE) != 0, 0}); movers_4 &= movers_4 - 1; }
            while(movers_5) { u64 t = 1ULL << (bitscan_forward(movers_5) - 1); moves.push_back({t >> 5, t, 0, (t & PROMO_RANK_WHITE) != 0, 0}); movers_5 &= movers_5 - 1; }
        } else {
            u64 men = board.black_men & ~board.kings;
            u64 movers_4 = ((men & NOT_COL_H) >> 4) & empty;
            u64 movers_5 = ((men & NOT_COL_A) >> 5) & empty;
            while(movers_4) { u64 t = 1ULL << (bitscan_forward(movers_4) - 1); moves.push_back({t << 4, t, 0, (t & PROMO_RANK_BLACK) != 0, 0}); movers_4 &= movers_4 - 1; }
            while(movers_5) { u64 t = 1ULL << (bitscan_forward(movers_5) - 1); moves.push_back({t << 5, t, 0, (t & PROMO_RANK_BLACK) != 0, 0}); movers_5 &= movers_5 - 1; }
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

    // --- Оценка и Применение хода ---
    const int PST[32] = { 10,10,10,10, 8,8,8,8, 6,6,6,6, 4,4,4,4, 2,2,2,2, 1,1,1,1, 0,0,0,0, 0,0,0,0 };

    int evaluate_giveaway(const Bitboard& b) {
        int white_material = popcount(b.white_men & ~b.kings) * 100 + popcount(b.white_men & b.kings) * 300;
        int black_material = popcount(b.black_men & ~b.kings) * 100 + popcount(b.black_men & b.kings) * 300;
        for (int i = 0; i < 32; ++i) {
            u64 mask = 1ULL << i;
            if (b.white_men & ~b.kings & mask) white_material += PST[i];
            if (b.black_men & ~b.kings & mask) black_material += PST[31 - i];
        }
        return black_material - white_material;
    }

    Bitboard apply_move(const Bitboard& b, const Move& m, int c) {
        Bitboard next_b = b;
        u64 from_to = m.mask_from | m.mask_to;
        next_b.hash = b.hash;

        int from_idx = bitscan_forward(m.mask_from) - 1;
        int to_idx = bitscan_forward(m.mask_to) - 1;
        bool is_king_before_move = (b.kings & m.mask_from) != 0;

        // 1. Обновляем битборды
        if (c == 1) { // White moves
            next_b.white_men ^= from_to;
            if (is_king_before_move) next_b.kings ^= from_to;
            if (m.captured_pieces) {
                next_b.black_men &= ~m.captured_pieces;
                next_b.kings &= ~m.captured_pieces;
            }
            if (m.becomes_king) next_b.kings |= m.mask_to;
        } else { // Black moves
            next_b.black_men ^= from_to;
            if (is_king_before_move) next_b.kings ^= from_to;
            if (m.captured_pieces) {
                next_b.white_men &= ~m.captured_pieces;
                next_b.kings &= ~m.captured_pieces;
            }
            if (m.becomes_king) next_b.kings |= m.mask_to;
        }

        // 2. Обновляем хеш
        int piece_type_from = (c == 1) ? (is_king_before_move ? 2 : 0) : (is_king_before_move ? 3 : 1);
        next_b.hash ^= ZOBRIST[from_idx][piece_type_from];
        // ИСПРАВЛЕНИЕ: Учитываем, что фигура может уже быть дамкой
        int piece_type_to = (c == 1) ? (m.becomes_king || is_king_before_move ? 2 : 0) : (m.becomes_king || is_king_before_move ? 3 : 1);
        next_b.hash ^= ZOBRIST[to_idx][piece_type_to];

        if (m.captured_pieces) {
            u64 captured = m.captured_pieces;
            while (captured) {
                int captured_idx = bitscan_forward(captured) - 1;
                bool was_king = (b.kings & (1ULL << captured_idx)) != 0;
                int captured_type = (c == 1) ? (was_king ? 3 : 1) : (was_king ? 2 : 0);
                next_b.hash ^= ZOBRIST[captured_idx][captured_type];
                captured &= captured - 1;
            }
        }
        next_b.hash ^= ZOBRIST_BLACK_TO_MOVE;
        return next_b;
    }

    // --- Логика Поиска ---
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
            return -MATE_SCORE + ply;
        }
        score_moves(moves, tt_entry.best_move, ply);

        int best_score = -INFINITY_SCORE;
        Move best_move = moves[0];
        TT_FLAG flag = TT_ALPHA;

        for (const auto& move : moves) {
            Bitboard next_board = apply_move(board, move, color);

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

        auto captures = generate_legal_moves(board, color);
        if (captures.empty() || captures[0].captured_pieces == 0 || ply > 8) {
            return stand_pat;
        }
        score_moves(captures, Move(), 0);

        for (const auto& capture : captures) {
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
