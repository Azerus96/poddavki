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
    try:
        while True:
            data = await websocket.receive_text()
            payload = json.loads(data)
            
            board = kestog_core.Bitboard()
            board.white_men = int(payload['board']['white_men'])
            board.black_men = int(payload['board']['black_men'])
            board.kings = int(payload['board']['kings'])

            if payload['type'] == 'engine_move':
                # Вызываем мощный C++ поиск
                result = kestog_core.find_best_move(board, BLACK, SEARCH_DEPTH, TIME_LIMIT_MS)
                
                if result.best_move.mask_from != 0:
                    new_board = kestog_core.apply_move(board, result.best_move, BLACK)
                    
                    player_moves = kestog_core.generate_legal_moves(new_board, WHITE)
                    if not player_moves:
                        await websocket.send_json({"type": "game_over", "message": "Вы проиграли (у вас нет ходов)!"})
                    else:
                        await websocket.send_json({
                            "type": "board_update", 
                            "board": {"white_men": str(new_board.white_men), "black_men": str(new_board.black_men), "kings": str(new_board.kings)}, 
                            "turn": WHITE,
                            "engine_info": f"Depth: {result.final_depth}, Score: {result.score}, Nodes: {result.nodes_searched}"
                        })
                else:
                    await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})

            elif payload['type'] == 'move':
                from_mask = 1 << payload['move']['from']
                to_mask = 1 << payload['move']['to']
                
                legal_moves = kestog_core.generate_legal_moves(board, WHITE)
                found_move = next((m for m in legal_moves if m.mask_from == from_mask and m.mask_to == to_mask), None)

                if found_move:
                    new_board = kestog_core.apply_move(board, found_move, WHITE)
                    
                    is_multicapture = False
                    if found_move.captured_pieces:
                        next_captures = kestog_core.generate_legal_moves(new_board, WHITE)
                        if next_captures and next_captures[0].captured_pieces > 0:
                            # Если после хода все еще есть взятия, и они начинаются с клетки, куда мы пришли
                            if any(cap.mask_from == found_move.mask_to for cap in next_captures):
                                is_multicapture = True
                    
                    if is_multicapture:
                        await websocket.send_json({"type": "board_update", "board": {"white_men": str(new_board.white_men), "black_men": str(new_board.black_men), "kings": str(new_board.kings)}, "turn": WHITE, "message": "Завершите взятие!"})
                    else:
                        engine_moves = kestog_core.generate_legal_moves(new_board, BLACK)
                        if not engine_moves:
                            await websocket.send_json({"type": "game_over", "message": "Вы победили (у движка нет ходов)!"})
                        else:
                             await websocket.send_json({"type": "board_update", "board": {"white_men": str(new_board.white_men), "black_men": str(new_board.black_men), "kings": str(new_board.kings)}, "turn": BLACK})
                else:
                    await websocket.send_json({"type": "error", "message": "Нелегальный ход!"})
    except WebSocketDisconnect:
        print("Client disconnected")
