# Forj — Open-Source Discord Replacement

A security-focused, self-hosted chat platform with a **Python (FastAPI) server** and a **C++ (Qt 6) desktop client**. Every account requires government ID + face-match KYC verification before access is granted.

---

## Architecture

```
client/  (C++/Qt 6)          server/  (Python/FastAPI)
┌────────────────────┐        ┌──────────────────────────┐
│  LoginWindow       │──REST──▶  /auth/register           │
│  KycDialog         │──REST──▶  /auth/kyc/initiate       │
│  MainWindow        │──REST──▶  /guilds  /channels  ...  │
│  ApiClient         │──REST──▶  /users  /dms  ...        │
│  WebSocketClient   │──WS────▶  /ws  (real-time events)  │
└────────────────────┘        └──────────────┬───────────┘
                                              │ SQLite (default)
                                              └──────────────────────
```

### Features
- User registration & JWT authentication with **token versioning**
- **KYC identity verification** — government ID + face match via [Didit](https://didit.me) (500 free/month). All API endpoints blocked until verified.
- **Servers (Guilds)** — create or join via invite code
- **Text channels** inside each server
- **Direct messages**
- Real-time messaging over WebSockets
- Online/offline presence indicators
- Member list with roles (owner, admin, member)
- **Content safety filtering** — regex pattern matching + conversation freeze on threshold breach
- Server profile avatars, roles, bans, invites
- Voice channel infrastructure (WebRTC-ready)

---

## Server Setup (Python)

### Requirements
- Python 3.11+
- A [Didit](https://didit.me) account (free tier — 500 verifications/month)

### Install & run

```bash
cd server
python -m venv .venv

# Windows
.venv\Scripts\Activate.ps1
# macOS / Linux
source .venv/bin/activate

pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000
```

### Configuration

Create `server/.env` with the following (never commit this file):

```env
DIDIT_API_KEY=your_didit_api_key
DIDIT_WORKFLOW_ID=your_kyc_workflow_uuid
DIDIT_WEBHOOK_SECRET=your_webhook_signing_secret
DIDIT_CALLBACK_URL=https://your-public-url/auth/didit/callback
```

Get these values from the [Didit console](https://console.didit.me):
1. **API Key** — Console → API Keys
2. **Workflow ID** — Console → Workflows → create a KYC workflow → copy the UUID
3. **Webhook Secret** — Console → Webhooks → Signing Secret
4. **Callback URL** — must be publicly reachable (use [Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/) or similar for local dev)

---

## Client Setup (C++/Qt 6)

### Requirements
- CMake 3.20+
- Qt 6.5+ (Widgets, Network, WebSockets, Multimedia modules)
- C++20 compiler — MSYS2 MinGW64 recommended on Windows
- MSYS2 path: `C:/msys64/mingw64`

### Build

```bash
cd client
# Linux/macOS (if Qt is discoverable)
cmake -B build -G Ninja

# Windows (MSYS2 example)
# cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/msys64/mingw64"

cmake --build build
```

### Run

```bash
./client/build/Forj
```

Windows:

```
client\build\Forj.exe
```

> To point the client at a different server, edit `src/main.cpp` (the `setBaseUrl` and `connectToServer` lines) and recompile.

### Pre-built client

A pre-built Windows client with all DLLs is available as `Forj-client.zip` in the repo root.

---

## Quick-start guide

1. Set up `server/.env` with Didit credentials.
2. Start the server: `uvicorn main:app --host 0.0.0.0 --port 8000`
3. Launch `Forj.exe` and **Register** — a KYC verification dialog will open automatically.
4. Complete identity verification in your browser (government ID + selfie).
5. The app unlocks once Didit confirms approval.
6. Click **+ New Server** to create your first server with a `#general` channel and invite code.
7. Share the invite code with friends.

---

## Project structure

```
Forj/
├── server/
│   ├── main.py               # FastAPI app + all routes
│   ├── models.py             # SQLAlchemy ORM models
│   ├── database.py           # Engine & session factory
│   ├── auth.py               # JWT helpers + dependency
│   ├── safety.py             # Content safety filtering
│   ├── websocket_manager.py  # Live connection registry
│   └── requirements.txt
└── client/
    ├── CMakeLists.txt
    └── src/
        ├── main.cpp                   # App entry + theme
        ├── ApiClient.{h,cpp}          # All REST calls
        ├── WebSocketClient.{h,cpp}    # Real-time events
        ├── LoginWindow.{h,cpp}        # Login / Register
        ├── KycDialog.{h,cpp}          # KYC verification wizard
        ├── MainWindow.{h,cpp}         # Main chat UI
        ├── ForjDialog.{h,cpp}         # Base themed dialog
        └── SettingsDialog.{h,cpp}     # User settings
```

---

## Security

- Every account requires **government ID + face match** (Didit KYC) before access
- **KYC middleware** blocks the entire API until verified
- Passwords hashed with **bcrypt**
- **JWT tokens with version claim** — password changes invalidate all existing sessions
- All SQL via **SQLAlchemy ORM** — no raw queries
- **HMAC-SHA256** webhook signature verification for Didit callbacks
- Content safety filtering with automatic conversation freeze
- Security hardening backlog checklist: [SECURITY_CHECKLIST.md](SECURITY_CHECKLIST.md)
- See [COMPLIANCE.md](COMPLIANCE.md) for full compliance status
