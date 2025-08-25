import json
import multiprocessing
import os
import time
import kestog_core
from fastapi import FastAPI, WebSocket
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketDisconnect

# --- Константы ---
WHITE, BLACK = 1, 2
MATE_SCORE = 10000
INFINITY = MATE_SCORE + 1
# Ограничим максимальное количество ядер для стабильности
MAX_CORES = min(16, os.cpu_count())

# --- Обёртка для многопроцессорного поиска ---
def search_task_wrapper(board_state, move_tuple, depth, color):
    board = kestog_core.Bitboard()
    board.white_men, board.black_men, board.kings = board_state
    move = kestog_core.Move()
    move.mask_from, move.mask_to, move.captured_pieces, move.becomes_king = move_tuple
    
    next_board = kestog_core.apply_move(board, move, color)
    score = -Engine._search(next_board, depth - 1, -INFINITY, INFINITY, BLACK if color == WHITE else WHITE)
    return score, move_tuple

# --- Класс Движка ---
class Engine:
    def __init__(self, num_cores):
        self.num_cores = num_cores
        # ИСПОЛЬЗУЕМ 'spawn' ДЛЯ КРОССПЛАТФОРМЕННОСТИ И СТАБИЛЬНОСТИ
        ctx = multiprocessing.get_context("spawn")
        self.pool = ctx.Pool(num_cores)

    def shutdown(self):
        print("Shutting down multiprocessing pool...")
        self.pool.close()
        self.pool.join()

    @staticmethod
    def _search(board, depth, alpha, beta, color):
        if depth == 0:
            return kestog_core.evaluate_giveaway(board, color)

        captures = kestog_core.generate_captures(board, color)
        if captures:
            # ПРАВИЛО МАКСИМАЛЬНОГО ВЗЯТИЯ
            max_captured = 0
            for m in captures:
                max_captured = max(max_captured, bin(m.captured_pieces).count('1'))
            
            moves = [m for m in captures if bin(m.captured_pieces).count('1') == max_captured]
        else:
            moves = kestog_core.generate_quiet_moves(board, color)

        if not moves:
            return -MATE_SCORE # Проигрыш для текущей стороны (нет ходов) -> выигрыш для оппонента

        for move in moves:
            next_board = kestog_core.apply_move(board, move, color)
            score = -Engine._search(next_board, depth - 1, -beta, -alpha, BLACK if color == WHITE else WHITE)
            if score >= beta:
                return beta
            if score > alpha:
                alpha = score
        return alpha

    def find_best_move(self, board, color, depth=10):
        captures = kestog_core.generate_captures(board, color)
        if captures:
            # ПРАВИЛО МАКСИМАЛЬНОГО ВЗЯТИЯ
            max_captured = max(bin(m.captured_pieces).count('1') for m in captures)
            moves = [m for m in captures if bin(m.captured_pieces).count('1') == max_captured]
        else:
            moves = kestog_core.generate_quiet_moves(board, color)

        if not moves: return None
        if len(moves) == 1: return moves[0]

        tasks = [((board.white_men, board.black_men, board.kings), 
                  (m.mask_from, m.mask_to, m.captured_pieces, m.becomes_king), 
                  depth, color) for m in moves]
        
        results = self.pool.starmap(search_task_wrapper, tasks)
        
        # Для поддавков ищем ход с МИНИМАЛЬНОЙ оценкой
        _, best_move_tuple = min(results, key=lambda item: item[0])
        
        best_move = kestog_core.Move()
        best_move.mask_from, best_move.mask_to, best_move.captured_pieces, best_move.becomes_king = best_move_tuple
        return best_move

# --- Веб-сервер FastAPI ---
app = FastAPI()
app.mount("/static", StaticFiles(directory="static"), name="static")
engine = Engine(num_cores=MAX_CORES)

@app.on_event("shutdown")
def shutdown_event():
    engine.shutdown()

@app.get("/")
async def read_root(): return FileResponse('static/index.html')

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        while True:
            data = await websocket.receive_text()
            payload = json.loads(data)
            board = kestog_core.Bitboard()
            board.white_men = int(payload['board']['white_men'])
            board.black_men = int(payload['board']['black_men'])
            board.kings = int(payload['board']['kings'])

            if payload['type'] == 'engine_move':
                best_move = engine.find_best_move(board, BLACK)
                if best_move:
                    new_board = kestog_core.apply_move(board, best_move, BLACK)
                    # Проверяем, есть ли у игрока ходы после хода движка
                    player_moves = kestog_core.generate_captures(new_board, WHITE)
                    if not player_moves: player_moves = kestog_core.generate_quiet_moves(new_board, WHITE)
                    
                    if not player_moves:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас нет ходов)!"})
                    else:
                        await websocket.send_json({"type": "board_update", "board": {"white_men": str(new_board.white_men), "black_men": str(new_board.black_men), "kings": str(new_board.kings)}, "turn": WHITE})
                else:
                    await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})

            elif payload['type'] == 'move':
                from_mask = 1 << payload['move']['from']
                to_mask = 1 << payload['move']['to']
                
                captures = kestog_core.generate_captures(board, WHITE)
                if captures:
                    max_captured = max(bin(m.captured_pieces).count('1') for m in captures)
                    legal_moves = [m for m in captures if bin(m.captured_pieces).count('1') == max_captured]
                else:
                    legal_moves = kestog_core.generate_quiet_moves(board, WHITE)

                found_move = next((m for m in legal_moves if m.mask_from == from_mask and m.mask_to == to_mask), None)

                if found_move:
                    new_board = kestog_core.apply_move(board, found_move, WHITE)
                    
                    # Проверяем, может ли эта же шашка бить дальше
                    is_multicapture = False
                    if found_move.captured_pieces:
                        next_captures = kestog_core.generate_captures(new_board, WHITE)
                        for cap in next_captures:
                            if cap.mask_from == found_move.mask_to:
                                is_multicapture = True
                                break
                    
                    if is_multicapture:
                        # Ход не завершен, отправляем промежуточное состояние
                        await websocket.send_json({"type": "board_update", "board": {"white_men": str(new_board.white_men), "black_men": str(new_board.black_men), "kings": str(new_board.kings)}, "turn": WHITE, "message": "Завершите взятие!"})
                    else:
                        # Ход завершен, проверяем, есть ли ходы у движка
                        engine_moves = kestog_core.generate_captures(new_board, BLACK)
                        if not engine_moves: engine_moves = kestog_core.generate_quiet_moves(new_board, BLACK)

                        if not engine_moves:
                            await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})
                        else:
                             await websocket.send_json({"type": "board_update", "board": {"white_men": str(new_board.white_men), "black_men": str(new_board.black_men), "kings": str(new_board.kings)}, "turn": BLACK})
                else:
                    await websocket.send_json({"type": "error", "message": "Нелегальный ход!"})
    except WebSocketDisconnect:
        print("Client disconnected")
