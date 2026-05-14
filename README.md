# Nexus — Open-Source Discord Replacement

A privacy-respecting, self-hosted chat platform with a **Python (FastAPI) server** and a **C++ (Qt 6) desktop client**.

---

## Architecture

```
client/  (C++/Qt 6)          server/  (Python/FastAPI)
┌────────────────────┐        ┌──────────────────────────┐
│  LoginWindow       │──REST──▶  /auth/register           │
│  MainWindow        │──REST──▶  /auth/login              │
│  ApiClient         │──REST──▶  /guilds  /channels  ...  │
│  WebSocketClient   │──WS────▶  /ws  (real-time events)  │
└────────────────────┘        └──────────────┬───────────┘
                                              │ SQLite (default)
                                              │ PostgreSQL (optional)
                                              └──────────────────────
```

### Features
- User registration & JWT authentication
- **Servers (Guilds)** — create or join via invite code
- **Text channels** inside each server
- Real-time messaging over WebSockets
- Online/offline presence indicators
- Member list with roles (owner, admin, member)

---

## Server Setup (Python)

### Requirements
- Python 3.11+

### Install & run

```bash
cd server
python -m venv .venv

# Windows
.venv\Scripts\activate
# macOS / Linux
source .venv/bin/activate

pip install -r requirements.txt

uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

The server starts at `http://localhost:8000`.  
Interactive API docs: `http://localhost:8000/docs`

### Configuration

| Variable       | Default                          | Description                        |
|----------------|----------------------------------|------------------------------------|
| `DATABASE_URL` | `sqlite:///./nexus.db`           | SQLAlchemy DB URL                  |
| `SECRET_KEY`   | hard-coded placeholder           | **Change before deploying!** (auth.py) |

To use PostgreSQL:
```bash
DATABASE_URL="postgresql://user:pass@localhost/nexus" uvicorn main:app --host 0.0.0.0 --port 8000
```

---

## Client Setup (C++/Qt 6)

### Requirements
- CMake 3.20+
- Qt 6.5+ (Widgets, Network, WebSockets modules)
- A C++20 compiler (MSVC 2022, GCC 12, or Clang 15+)

### Build

```bash
cd client
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"   # adjust path
cmake --build build --config Release
```

On Linux/macOS, Qt is often found automatically:
```bash
cmake -B build && cmake --build build
```

### Run

```bash
# Windows
build\Release\Nexus.exe

# Linux / macOS
./build/Nexus
```

> By default the client connects to `http://localhost:8000`. To change the server
> address, edit `main.cpp` and recompile.

---

## Quick-start guide

1. Start the server.
2. Launch the client and **Register** a new account.
3. Click **+ New Server** to create your first server — it gets a `#general` channel and an invite code automatically.
4. Share the invite code with friends; they click **Join Server** and enter the code.
5. Select a channel and start chatting in real time.

---

## Project structure

```
Discord_Replacement/
├── server/
│   ├── main.py               # FastAPI app + all routes
│   ├── models.py             # SQLAlchemy ORM models
│   ├── database.py           # Engine & session factory
│   ├── auth.py               # JWT helpers + dependency
│   ├── websocket_manager.py  # Live connection registry
│   └── requirements.txt
└── client/
    ├── CMakeLists.txt
    └── src/
        ├── main.cpp           # App entry + dark theme
        ├── ApiClient.{h,cpp}  # All REST calls
        ├── WebSocketClient.{h,cpp}  # Real-time events
        ├── LoginWindow.{h,cpp}      # Login / Register dialog
        └── MainWindow.{h,cpp}       # Main chat UI
```

---

## Security notes

- Passwords are hashed with **bcrypt** (passlib).
- Sessions use signed **JWT** tokens (python-jose / HS256).
- All SQL access goes through **SQLAlchemy ORM** — no raw queries.
- HTML in messages is escaped client-side (`toHtmlEscaped()`).
- Change `SECRET_KEY` in `server/auth.py` before any public deployment.
