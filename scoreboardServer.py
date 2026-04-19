#!/usr/bin/env python3
"""
Fencing Scoreboard Server
=========================
Run: python3 scoreboard_server.py [port]
Default port: 8765

CLI Commands (type in terminal after server starts):
  l_valid     - Left fencer valid hit (green light)
  r_valid     - Right fencer valid hit (green light)
  l_off       - Left fencer off-target hit (white light)
  r_off       - Right fencer off-target hit (white light)
  clear       - Clear all lights
  l_score+    - Increment left score
  l_score-    - Decrement left score
  r_score+    - Increment right score
  r_score-    - Decrement right score
  l_yellow    - Give left fencer yellow card
  l_red       - Give left fencer red card
  l_black     - Give left fencer black card
  r_yellow    - Give right fencer yellow card
  r_red       - Give right fencer red card
  r_black     - Give right fencer black card
  l_pcard     - Toggle left fencer P-card
  r_pcard     - Toggle right fencer P-card
  start       - Start/resume timer
  stop        - Pause timer
  reset_time  - Reset timer to 3:00
  reset_all   - Reset entire scoreboard
  set_time MM:SS - Set timer (e.g. set_time 2:30)
  help        - Show this help
"""

import asyncio
import json
import sys
import threading
import time
import websockets
from websockets.server import serve

# -- State ----------------------------------------------------------------------
state = {
    "lights": {
        "left_valid": False,
        "right_valid": False,
        "left_off": False,
        "right_off": False,
    },
    "scores": {"left": 0, "right": 0},
    "cards": {
        "left":  {"yellow": 0, "red": 0, "black": False},
        "right": {"yellow": 0, "red": 0, "black": False},
    },
    "pcards": {"left": False, "right": False},
    "timer": {"seconds": 180, "running": False},
    "period": 1,
}

clients = set()
timer_task = None
loop = None  # main event loop reference

# -- Broadcast ------------------------------------------------------------------
async def broadcast(msg: dict):
    if clients:
        data = json.dumps(msg)
        await asyncio.gather(*[c.send(data) for c in list(clients)], return_exceptions=True)

def broadcast_sync(msg: dict):
    """Thread-safe broadcast from non-async context."""
    if loop and not loop.is_closed():
        asyncio.run_coroutine_threadsafe(broadcast(msg), loop)

# -- Timer ----------------------------------------------------------------------
async def run_timer():
    global state
    while state["timer"]["running"] and state["timer"]["seconds"] > 0:
        await asyncio.sleep(1)
        if state["timer"]["running"]:
            state["timer"]["seconds"] -= 1
            await broadcast({"type": "state", "data": state})
            if state["timer"]["seconds"] == 0:
                state["timer"]["running"] = False
                await broadcast({"type": "buzzer"})

async def start_timer():
    global timer_task
    if not state["timer"]["running"] and state["timer"]["seconds"] > 0:
        state["timer"]["running"] = True
        timer_task = asyncio.create_task(run_timer())
        await broadcast({"type": "state", "data": state})

async def stop_timer():
    global timer_task
    state["timer"]["running"] = False
    if timer_task:
        timer_task.cancel()
    await broadcast({"type": "state", "data": state})

# -- Light auto-clear -----------------------------------------------------------
async def auto_clear_lights(delay=3.0):
    await asyncio.sleep(delay)
    state["lights"] = {k: False for k in state["lights"]}
    await broadcast({"type": "state", "data": state})

# -- Command handler (async) ----------------------------------------------------
async def handle_command(cmd: str):
    cmd = cmd.strip().lower()
    parts = cmd.split()
    if not parts:
        return

    verb = parts[0]

    light_map = {
        "l_valid": "left_valid",
        "r_valid": "right_valid",
        "l_off":   "left_off",
        "r_off":   "right_off",
    }

    if verb in light_map:
        state["lights"] = {k: False for k in state["lights"]}
        state["lights"][light_map[verb]] = True
        await broadcast({"type": "state", "data": state})
        asyncio.create_task(auto_clear_lights())

    elif verb == "clear":
        state["lights"] = {k: False for k in state["lights"]}
        await broadcast({"type": "state", "data": state})

    elif verb == "l_score+":
        state["scores"]["left"] = min(99, state["scores"]["left"] + 1)
        await broadcast({"type": "state", "data": state})
    elif verb == "l_score-":
        state["scores"]["left"] = max(0, state["scores"]["left"] - 1)
        await broadcast({"type": "state", "data": state})
    elif verb == "r_score+":
        state["scores"]["right"] = min(99, state["scores"]["right"] + 1)
        await broadcast({"type": "state", "data": state})
    elif verb == "r_score-":
        state["scores"]["right"] = max(0, state["scores"]["right"] - 1)
        await broadcast({"type": "state", "data": state})

    elif verb == "l_yellow":
        state["cards"]["left"]["yellow"] = min(2, state["cards"]["left"]["yellow"] + 1)
        await broadcast({"type": "state", "data": state})
    elif verb == "l_red":
        state["cards"]["left"]["red"] = min(2, state["cards"]["left"]["red"] + 1)
        await broadcast({"type": "state", "data": state})
    elif verb == "l_black":
        state["cards"]["left"]["black"] = not state["cards"]["left"]["black"]
        await broadcast({"type": "state", "data": state})
    elif verb == "r_yellow":
        state["cards"]["right"]["yellow"] = min(2, state["cards"]["right"]["yellow"] + 1)
        await broadcast({"type": "state", "data": state})
    elif verb == "r_red":
        state["cards"]["right"]["red"] = min(2, state["cards"]["right"]["red"] + 1)
        await broadcast({"type": "state", "data": state})
    elif verb == "r_black":
        state["cards"]["right"]["black"] = not state["cards"]["right"]["black"]
        await broadcast({"type": "state", "data": state})

    elif verb == "l_pcard":
        state["pcards"]["left"] = not state["pcards"]["left"]
        await broadcast({"type": "state", "data": state})
    elif verb == "r_pcard":
        state["pcards"]["right"] = not state["pcards"]["right"]
        await broadcast({"type": "state", "data": state})

    elif verb == "start":
        await start_timer()
    elif verb == "stop":
        await stop_timer()
    elif verb == "reset_time":
        await stop_timer()
        state["timer"]["seconds"] = 180
        await broadcast({"type": "state", "data": state})
    elif verb == "set_time" and len(parts) == 2:
        try:
            m, s = parts[1].split(":")
            state["timer"]["seconds"] = int(m) * 60 + int(s)
            state["timer"]["running"] = False
            await broadcast({"type": "state", "data": state})
        except Exception:
            print("Usage: set_time MM:SS  (e.g. set_time 2:30)")

    elif verb == "reset_all":
        await stop_timer()
        state["lights"]  = {k: False for k in state["lights"]}
        state["scores"]  = {"left": 0, "right": 0}
        state["cards"]   = {"left": {"yellow": 0, "red": 0, "black": False},
                             "right": {"yellow": 0, "red": 0, "black": False}}
        state["pcards"]  = {"left": False, "right": False}
        state["timer"]   = {"seconds": 180, "running": False}
        state["period"]  = 1
        await broadcast({"type": "state", "data": state})

    elif verb == "help":
        print(__doc__)

    else:
        print(f"Unknown command: '{cmd}'. Type 'help' for commands.")

# -- WebSocket handler ----------------------------------------------------------
async def ws_handler(ws):
    clients.add(ws)
    print(f"[+] Client connected ({len(clients)} total)")
    try:
        await ws.send(json.dumps({"type": "state", "data": state}))
        async for raw in ws:
            try:
                msg = json.loads(raw)
                if msg.get("type") == "command":
                    await handle_command(msg["cmd"])
            except json.JSONDecodeError:
                pass
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        clients.discard(ws)
        print(f"[-] Client disconnected ({len(clients)} total)")

# -- CLI input thread -----------------------------------------------------------
def cli_thread():
    print("Fencing Scoreboard ready. Type 'help' for commands.\n")
    while True:
        try:
            cmd = input()
            if cmd and loop and not loop.is_closed():
                asyncio.run_coroutine_threadsafe(handle_command(cmd), loop)
        except EOFError:
            break
        except KeyboardInterrupt:
            break

# -- Main -----------------------------------------------------------------------
async def main():
    global loop
    loop = asyncio.get_running_loop()

    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765

    t = threading.Thread(target=cli_thread, daemon=True)
    t.start()

    print(f"WebSocket server listening on ws://localhost:{port}")
    print(f"Open scoreboard.html in your browser.\n")

    async with serve(ws_handler, "localhost", port):
        await asyncio.Future()  # run forever

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nServer stopped.")
