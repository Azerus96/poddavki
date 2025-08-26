import json
import os
import kestog_core
from fastapi import FastAPI, WebSocket
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketDisconnect

# --- Константы ---
WHITE, BLACK = 1, 2
TT_SIZE_MB = 128 # Размер транспозиционной таблицы в мегабайтах
SEARCH_DEPTH = 16 # Максимальная глубина поиска
TIME_LIMIT_MS = 5000 # 5 секунд на ход

# Таблица для перевода из 64-клеточной нумерации фронтенда (JS)
# в 32-клеточную нумерацию движка (C++).
LOOKUP_64_TO_32 = {
    1: 0, 3: 1, 5: 2, 7: 3,
    8: 4, 10: 5, 12: 6, 14: 7,
    17: 8, 19: 9, 21: 10, 23: 11,
    24: 12, 26: 13, 28: 14, 30: 15,
    33: 16, 35: 17, 37: 18, 39: 19,
    40: 20, 42: 21, 44: 22, 46: 23,
    49: 24, 51: 25, 53: 26, 55: 27,
    56: 28, 58: 29, 60: 30, 62: 31
}

# --- Инициализация C++ движка ---
kestog_core.init_engine(TT_SIZE_MB)

# --- Веб-сервер FastAPI ---
app = FastAPI()
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def read_root(): return FileResponse('static/index.html')

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    
    # Сервер является "хозяином" игры. Он создает и хранит состояние доски.
    initial_board = kestog_core.Bitboard()
    initial_board.white_men = 4095
    initial_board.black_men = 4293918720
    initial_board.kings = 0
    current_board = initial_board

    # Отправляем первое состояние доски клиенту, чтобы начать игровой цикл.
    await websocket.send_json({
        "type": "board_update",
        "board": {
            "white_men": str(current_board.white_men),
            "black_men": str(current_board.black_men),
            "kings": str(current_board.kings)
        },
        "turn": WHITE,
        "message": "Ваш ход"
    })

    try:
        while True:
            data = await websocket.receive_text()
            payload = json.loads(data)
            
            if payload['type'] == 'engine_move':
                result = kestog_core.find_best_move(current_board, BLACK, SEARCH_DEPTH, TIME_LIMIT_MS)
                
                if result.best_move.mask_from != 0:
                    current_board = kestog_core.apply_move(current_board, result.best_move, BLACK)
                    
                    # Проверка на победу/поражение
                    if not current_board.white_men:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас не осталось шашек)!"})
                        continue
                    
                    player_moves = kestog_core.generate_legal_moves(current_board, WHITE)
                    if not player_moves:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас нет ходов)!"})
                    else:
                        await websocket.send_json({
                            "type": "board_update", 
                            "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, 
                            "turn": WHITE,
                            "engine_info": f"Depth: {result.final_depth}, Score: {result.score}, Nodes: {result.nodes_searched}"
                        })
                else:
                    await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})

            elif payload['type'] == 'move':
                try:
                    from_64 = payload['move']['from']
                    to_64 = payload['move']['to']
                    from_32 = LOOKUP_64_TO_32[from_64]
                    to_32 = LOOKUP_64_TO_32[to_64]
                    from_mask = 1 << from_32
                    to_mask = 1 << to_32
                except KeyError:
                    await websocket.send_json({"type": "error", "message": "Неверные координаты хода!"})
                    continue
                
                legal_moves = kestog_core.generate_legal_moves(current_board, WHITE)
                found_move = next((m for m in legal_moves if m.mask_from == from_mask and m.mask_to == to_mask), None)

                if found_move:
                    current_board = kestog_core.apply_move(current_board, found_move, WHITE)
                    
                    # Проверка на победу/поражение
                    if not current_board.black_men:
                        await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка не осталось шашек)!"})
                        continue

                    is_multicapture = False
                    if found_move.captured_pieces:
                        next_captures = kestog_core.generate_legal_moves(current_board, WHITE)
                        if next_captures and next_captures[0].captured_pieces > 0:
                            if any(cap.mask_from == found_move.mask_to for cap in next_captures):
                                is_multicapture = True
                    
                    if is_multicapture:
                        await websocket.send_json({
                            "type": "board_update", 
                            "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, 
                            "turn": WHITE, 
                            "message": "Завершите взятие!",
                            "must_move_from": to_32 # Отправляем индекс клетки для подсветки
                        })
                    else:
                        engine_moves = kestog_core.generate_legal_moves(current_board, BLACK)
                        if not engine_moves:
                            await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})
                        else:
                             await websocket.send_json({
                                 "type": "board_update", 
                                 "board": {"white_men": str(current_board.white_men), "black_men": str(current_board.black_men), "kings": str(current_board.kings)}, 
                                 "turn": BLACK
                             })
                else:
                    await websocket.send_json({"type": "error", "message": "Нелегальный ход!"})
    except WebSocketDisconnect:
        print("Client disconnected")
