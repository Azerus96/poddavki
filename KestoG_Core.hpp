#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace kestog_core {
    using u64 = uint64_t;

    // --- Структуры данных ---
    struct Bitboard {
        u64 white_men;
        u64 black_men;
        u64 kings;
        u64 hash; // Zobrist hash
    };

    struct Move {
        u64 mask_from;
        u64 mask_to;
        u64 captured_pieces;
        bool becomes_king;
        int score; // Для упорядочивания ходов
    };

    // --- Транспозиционная таблица ---
    enum TT_FLAG { TT_EXACT, TT_ALPHA, TT_BETA };
    struct TT_Entry {
        u64 hash_lock;
        int score;
        int depth;
        TT_FLAG flag;
        Move best_move;
    };

    // --- Структура для передачи результатов поиска ---
    struct SearchResult {
        Move best_move;
        int score;
        long long nodes_searched;
        double time_taken_ms;
        int final_depth;
    };

    // --- Основные функции, вызываемые из Python ---

    // Инициализация движка (Zobrist ключи, ТТ)
    void init_engine(int tt_size_mb);

    // Главная функция поиска лучшего хода
    SearchResult find_best_move(const Bitboard& board, int color_to_move, int max_depth, int time_limit_ms);

    // Функции для генерации ходов (остаются для валидации и UI)
    std::vector<Move> generate_legal_moves(const Bitboard& board, int color_to_move);
    Bitboard apply_move(const Bitboard& board, const Move& move, int color_to_move);
    
    // Вспомогательная функция для создания хеша с нуля
    u64 calculate_hash(const Bitboard& board, int color_to_move);
}
