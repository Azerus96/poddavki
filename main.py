import json
import os
import kestog_core
from fastapi import FastAPI, WebSocket
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from starlette.websockets import WebSocketDisconnect

# --- Константы ---
WHITE, BLACK = 1, 2
TT_SIZE_MB = 128
SEARCH_DEPTH = 16
TIME_LIMIT_MS = 5000

# --- ТАБЛИЦА КООРДИНАТ УДАЛЕНА ---
# Фронтенд и бэкенд теперь общаются на одном языке (индексы 0-31)

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
    
    initial_board = kestog_core.Bitboard()
    initial_board.white_men = 4095
    initial_board.black_men = 4293918720 # Корректное значение
    initial_board.kings = 0
    current_board = initial_board

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
                    from_32 = int(payload['move']['from'])
                    to_32 = int(payload['move']['to'])

                    # Валидация полученных индексов
                    if not (0 <= from_32 < 32 and 0 <= to_32 < 32):
                        await websocket.send_json({"type": "error", "message": "Неверные координаты хода!"})
                        continue

                    from_mask = 1 << from_32
                    to_mask = 1 << to_32
                except (KeyError, ValueError, TypeError):
                    await websocket.send_json({"type": "error", "message": "Неверный формат хода!"})
                    continue
                
                legal_moves = kestog_core.generate_legal_moves(current_board, WHITE)
                found_move = next((m for m in legal_moves if m.mask_from == from_mask and m.mask_to == to_mask), None)

                if found_move:
                    current_board = kestog_core.apply_move(current_board, found_move, WHITE)
                    
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
                            "must_move_from": to_32
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
