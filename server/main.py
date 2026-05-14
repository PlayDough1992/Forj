"""
Forj – Discord replacement server
Run from the server/ directory:
    uvicorn main:app --reload --host 0.0.0.0 --port 8000
"""

import asyncio
import hashlib
import hmac
import json
import logging
import os
import re
import secrets
import uuid

logger = logging.getLogger("forj")
from datetime import date, datetime, timezone
from typing import List, Optional

import httpx
from dotenv import load_dotenv
from fastapi import Depends, FastAPI, File, HTTPException, Request, UploadFile, WebSocket, WebSocketDisconnect, status
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel, ConfigDict, EmailStr, Field, field_validator
from slowapi import Limiter, _rate_limit_exceeded_handler
from slowapi.errors import RateLimitExceeded
from slowapi.util import get_remote_address
from sqlalchemy.orm import Session
from sqlalchemy import or_

from auth import (
    create_access_token, create_bot_token,
    get_current_user, hash_password, verify_password, verify_token,
    SECRET_KEY, ALGORITHM,
)
from database import engine, get_db
from models import (
    Base, Ban, Bot, Channel, DirectConversation, DirectMessage,
    Friendship, FrozenConversation, Guild, GuildMember, MemberRole, Message, Perms, Role,
    SafetyFlag, ServerProfile, SlashCommand, User, Webhook,
)
from safety import analyze_message, build_reason, FREEZE_THRESHOLD
from websocket_manager import manager

load_dotenv()

# ── Didit KYC config ──────────────────────────────────────────────────────────
DIDIT_API_KEY       = os.environ.get("DIDIT_API_KEY", "")
DIDIT_WORKFLOW_ID   = os.environ.get("DIDIT_WORKFLOW_ID", "")
DIDIT_WEBHOOK_SECRET = os.environ.get("DIDIT_WEBHOOK_SECRET", "")
DIDIT_BASE_URL      = "https://verification.didit.me/v3"

# Resolved at startup
_system_user_id: int = 0

# ── Voice state (in-memory) ───────────────────────────────────────────────────
# channel_id -> {user_id: username}
_voice_state: dict[int, dict[int, str]] = {}

Base.metadata.create_all(bind=engine)


def _migrate_db() -> None:
    """Add columns introduced after initial schema without losing data."""
    import sqlite3 as _sqlite3
    db_path = os.path.join(os.path.dirname(__file__), "forj.db")
    if not os.path.exists(db_path):
        return
    conn = _sqlite3.connect(db_path)
    cur  = conn.cursor()

    # ── messages ──────────────────────────────────────────────────────────────
    cur.execute("PRAGMA table_info(messages)")
    msg_cols = {r[1] for r in cur.fetchall()}
    if "reply_to_id" not in msg_cols:
        cur.execute("ALTER TABLE messages ADD COLUMN reply_to_id INTEGER")

    # ── direct_messages ───────────────────────────────────────────────────────
    cur.execute("PRAGMA table_info(direct_messages)")
    dm_cols = {r[1] for r in cur.fetchall()}
    if "reply_to_id" not in dm_cols:
        cur.execute("ALTER TABLE direct_messages ADD COLUMN reply_to_id INTEGER")

    # ── users ─────────────────────────────────────────────────────────────────
    cur.execute("PRAGMA table_info(users)")
    user_cols = {r[1] for r in cur.fetchall()}
    if "status" not in user_cols:
        cur.execute("ALTER TABLE users ADD COLUMN status TEXT DEFAULT 'online'")
    # KYC — default 'approved' so existing accounts aren't locked out
    if "kyc_status" not in user_cols:
        cur.execute("ALTER TABLE users ADD COLUMN kyc_status TEXT DEFAULT 'approved'")
    if "kyc_session_id" not in user_cols:
        cur.execute("ALTER TABLE users ADD COLUMN kyc_session_id TEXT")
    if "date_of_birth" not in user_cols:
        cur.execute("ALTER TABLE users ADD COLUMN date_of_birth TEXT")
    if "token_version" not in user_cols:
        cur.execute("ALTER TABLE users ADD COLUMN token_version INTEGER DEFAULT 0")

    conn.commit()
    conn.close()


_migrate_db()

# ── Rate limiter ──────────────────────────────────────────────────────────────
limiter = Limiter(key_func=get_remote_address)

app = FastAPI(title="Forj API", version="1.0.0")
app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],    # Restrict in production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── KYC enforcement middleware ────────────────────────────────────────────────
# Paths that are always accessible regardless of KYC status
_KYC_EXEMPT_PREFIXES = (
    "/auth/",
    "/ws",
    "/docs",
    "/openapi",
    "/redoc",
)

@app.middleware("http")
async def kyc_required_middleware(request: Request, call_next):
    path = request.url.path
    # Skip KYC check for exempt paths
    if any(path.startswith(p) for p in _KYC_EXEMPT_PREFIXES):
        return await call_next(request)

    auth_header = request.headers.get("Authorization", "")
    if not auth_header.startswith("Bearer "):
        return await call_next(request)   # No token — let auth dependency reject it

    token = auth_header[7:]
    try:
        from jose import jwt as _jwt
        payload  = _jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        user_id  = int(payload.get("sub", 0))
    except Exception:
        return await call_next(request)   # Invalid token — let auth dependency reject it

    # Bots and the system user bypass KYC
    db = next(get_db())
    try:
        user = db.query(User).filter(User.id == user_id, User.is_active == True).first()
        if user and not user.is_bot and (user.kyc_status or "unverified") != "approved":
            return JSONResponse(
                status_code=403,
                content={
                    "detail": (
                        "Identity verification required. "
                        "Complete KYC at POST /auth/kyc/initiate to access Forj."
                    ),
                    "kyc_status": user.kyc_status or "unverified",
                },
            )
    finally:
        db.close()

    return await call_next(request)


# ── Startup: system user + Didit workflow fetch ───────────────────────────────
@app.on_event("startup")
async def _on_startup():
    global _system_user_id
    db = next(get_db())
    try:
        system = db.query(User).filter(User.username == "ForjSystem").first()
        if not system:
            system = User(
                username="ForjSystem",
                email="system@forj.internal",
                hashed_password="",
                display_name="Forj Administration",
                is_bot=True,
                is_active=True,
                kyc_status="approved",
            )
            db.add(system)
            db.commit()
            db.refresh(system)
        _system_user_id = system.id
    finally:
        db.close()


# ── Helpers ───────────────────────────────────────────────────────────────────

def _user_age(user: User) -> Optional[int]:
    """Return user's current age in years, or None if DOB is unknown."""
    if not user.date_of_birth:
        return None
    try:
        dob = date.fromisoformat(user.date_of_birth)
        today = date.today()
        return today.year - dob.year - ((today.month, today.day) < (dob.month, dob.day))
    except ValueError:
        return None


def _is_minor(user: User) -> Optional[bool]:
    age = _user_age(user)
    return age < 18 if age is not None else None


# ── Pydantic schemas ───────────────────────────────────────────────────────────

class UserRegister(BaseModel):
    username: str = Field(..., min_length=2, max_length=32, pattern=r"^[a-zA-Z0-9_]+$")
    email: EmailStr
    password: str = Field(..., min_length=8, max_length=128)


class UserLogin(BaseModel):
    username: str = Field(..., max_length=32)
    password: str = Field(..., max_length=128)


class UserOut(BaseModel):
    id: int
    username: str
    email: str
    display_name: Optional[str]
    bio: Optional[str]
    has_avatar: bool
    is_bot: bool
    created_at: datetime
    status: str = "online"
    kyc_status: str = "unverified"


class KycInitiateOut(BaseModel):
    session_id: str
    verification_url: str
    message: str


class TokenOut(BaseModel):
    access_token: str
    token_type: str
    user: UserOut
    kyc_required: bool = False
    kyc_session_id: Optional[str] = None
    kyc_verification_url: Optional[str] = None


class GuildCreate(BaseModel):
    name: str = Field(..., min_length=1, max_length=100)


class GuildOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)
    id: int
    name: str
    owner_id: int
    invite_code: str
    has_icon: bool = False
    created_at: datetime


class GuildUpdate(BaseModel):
    name: str = Field(..., min_length=1, max_length=100)


class GuildJoin(BaseModel):
    invite_code: str = Field(..., min_length=1, max_length=32)


class ChannelCreate(BaseModel):
    name: str = Field(..., min_length=1, max_length=100, pattern=r"^[a-z0-9_-]+$")
    channel_type: str = Field("text", pattern=r"^(text|voice)$")


class ChannelOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)
    id: int
    guild_id: int
    name: str
    channel_type: str
    created_at: datetime


class MessageCreate(BaseModel):
    content: str = Field(..., min_length=1, max_length=4000)
    reply_to_id: Optional[int] = None

    @field_validator("content")
    @classmethod
    def strip_content(cls, v: str) -> str:
        return v.strip()


class MessageOut(BaseModel):
    id: int
    channel_id: int
    author_id: int
    author_username: str
    author_has_avatar: bool = False
    author_is_bot: bool = False
    content: str
    reply_to_id: Optional[int] = None
    reply_to_author: Optional[str] = None
    reply_to_content: Optional[str] = None
    created_at: datetime
    edited_at: Optional[datetime]


class MemberOut(BaseModel):
    user_id: int
    username: str
    display_name: Optional[str]
    nickname: Optional[str] = None
    role: str
    is_online: bool
    has_avatar: bool = False
    is_bot: bool = False
    roles: List["RoleOut"] = []
    status: str = "offline"


class RoleOut(BaseModel):
    id: int
    guild_id: int
    name: str
    color: str
    permissions: int
    position: int
    hoist: bool


class RoleCreate(BaseModel):
    name: str = Field(..., min_length=1, max_length=50)
    color: str = Field(default="#99AAB5", pattern=r"^#[0-9a-fA-F]{6}$")
    permissions: int = Field(default=0, ge=0)
    position: int = Field(default=0, ge=0)
    hoist: bool = False


class RoleUpdate(BaseModel):
    name: Optional[str] = Field(None, min_length=1, max_length=50)
    color: Optional[str] = Field(None, pattern=r"^#[0-9a-fA-F]{6}$")
    permissions: Optional[int] = Field(None, ge=0)
    position: Optional[int] = Field(None, ge=0)
    hoist: Optional[bool] = None


class ServerProfileUpdate(BaseModel):
    nickname: Optional[str] = Field(None, max_length=32)


class ServerProfileOut(BaseModel):
    guild_id: int
    user_id: int
    nickname: Optional[str]
    has_avatar: bool


class BanCreate(BaseModel):
    reason: Optional[str] = Field(None, max_length=255)


class BanOut(BaseModel):
    guild_id: int
    user_id: int
    username: str
    reason: Optional[str]


# ── New schemas ────────────────────────────────────────────────────────────────

class UserProfileOut(BaseModel):
    id: int
    username: str
    display_name: Optional[str]
    bio: Optional[str]
    is_bot: bool
    has_avatar: bool


class UserUpdate(BaseModel):
    display_name: Optional[str] = Field(None, max_length=32)
    bio: Optional[str] = Field(None, max_length=500)


class PasswordChange(BaseModel):
    current_password: str = Field(..., max_length=128)
    new_password: str = Field(..., min_length=8, max_length=128)


class StatusUpdate(BaseModel):
    status: str = Field(..., pattern=r"^(online|idle|dnd|invisible)$")


class DmCreate(BaseModel):
    user_id: int


class DmMessageCreate(BaseModel):
    content: str = Field(..., min_length=1, max_length=4000)
    reply_to_id: Optional[int] = None

    @field_validator("content")
    @classmethod
    def strip_content(cls, v: str) -> str:
        return v.strip()


class DmConversationOut(BaseModel):
    id: int
    other_user_id: int
    other_username: str
    other_display_name: Optional[str]
    other_has_avatar: bool


class DmMessageOut(BaseModel):
    id: int
    conversation_id: int
    author_id: int
    author_username: str
    author_has_avatar: bool = False
    author_is_bot: bool = False
    content: str
    reply_to_id: Optional[int] = None
    reply_to_author: Optional[str] = None
    reply_to_content: Optional[str] = None
    created_at: datetime


class BotCreate(BaseModel):
    name: str = Field(..., min_length=2, max_length=32, pattern=r"^[a-zA-Z0-9_\- ]+$")


class BotOut(BaseModel):
    id: int
    user_id: int
    username: str
    name: str


class BotCreatedOut(BotOut):
    token: str   # shown ONCE at creation


class CommandCreate(BaseModel):
    name: str = Field(..., min_length=1, max_length=32, pattern=r"^[a-z0-9_-]+$")
    description: str = Field(..., min_length=1, max_length=200)


class CommandOut(BaseModel):
    id: int
    guild_id: int
    name: str
    description: str
    bot_username: str


class CommandInvoke(BaseModel):
    name: str = Field(..., max_length=32)
    args: str = Field("", max_length=1000)


class WebhookCreate(BaseModel):
    name: str = Field(..., min_length=1, max_length=80)


class WebhookOut(BaseModel):
    id: int
    channel_id: int
    name: str
    token: str
    url: str


class WebhookPost(BaseModel):
    content: str = Field(..., min_length=1, max_length=4000)
    username: Optional[str] = Field(None, max_length=80)


class FriendOut(BaseModel):
    user_id: int
    username: str
    display_name: Optional[str]
    has_avatar: bool
    status: str = "offline"


class FriendRequestOut(BaseModel):
    from_user_id: int
    from_username: str
    from_display_name: Optional[str]
    from_has_avatar: bool


# ── Auth routes ────────────────────────────────────────────────────────────────

@app.post("/auth/register", response_model=TokenOut, status_code=status.HTTP_201_CREATED)
@limiter.limit("5/minute")
async def register(request: Request, data: UserRegister, db: Session = Depends(get_db)):
    if db.query(User).filter(User.username == data.username).first():
        raise HTTPException(status_code=400, detail="Username already taken")
    if db.query(User).filter(User.email == data.email).first():
        raise HTTPException(status_code=400, detail="Email already registered")

    user = User(
        username=data.username,
        email=data.email,
        hashed_password=hash_password(data.password),
        display_name=data.username,
        kyc_status="unverified",
    )
    db.add(user)
    db.commit()
    db.refresh(user)

    token = create_access_token(user.id, user.token_version or 0)

    # Attempt to create a Didit KYC session immediately
    kyc_url: Optional[str] = None
    kyc_sid: Optional[str] = None
    if DIDIT_API_KEY and DIDIT_WORKFLOW_ID:
        try:
            async with httpx.AsyncClient(timeout=10) as client:
                resp = await client.post(
                    f"{DIDIT_BASE_URL}/session/",
                    headers={"x-api-key": DIDIT_API_KEY, "content-type": "application/json"},
                    json={
                        "workflow_id": DIDIT_WORKFLOW_ID,
                        "vendor_data": str(user.id),
                        "callback": os.environ.get("DIDIT_CALLBACK_URL",
                                                    "https://yourapp.example.com/auth/didit/webhook"),
                    },
                )
                if resp.status_code in (200, 201):
                    body = resp.json()
                    kyc_sid = body.get("session_id")
                    kyc_url = body.get("url") or body.get("verification_url")
                    user.kyc_session_id = kyc_sid
                    user.kyc_status     = "pending"
                    db.commit()
                else:
                    logger.error("Didit session create error %s: %s", resp.status_code, resp.text)
        except Exception:
            pass  # Non-fatal: user can retry via /auth/kyc/initiate

    return TokenOut(
        access_token=token,
        token_type="bearer",
        user=_user_out(user),
        kyc_required=True,
        kyc_session_id=kyc_sid,
        kyc_verification_url=kyc_url,
    )


@app.post("/auth/login", response_model=TokenOut)
@limiter.limit("10/minute")
def login(request: Request, data: UserLogin, db: Session = Depends(get_db)):
    user = db.query(User).filter(User.username == data.username, User.is_active == True).first()
    if not user or not verify_password(data.password, user.hashed_password):
        raise HTTPException(status_code=401, detail="Invalid username or password")

    token = create_access_token(user.id, user.token_version or 0)
    return TokenOut(
        access_token=token,
        token_type="bearer",
        user=_user_out(user),
        kyc_required=(user.kyc_status or "unverified") != "approved",
    )


@app.get("/users/me", response_model=UserOut)
def get_me(current_user: User = Depends(get_current_user)):
    return _user_out(current_user)


# ── KYC routes ────────────────────────────────────────────────────────────────

@app.post("/auth/kyc/initiate", response_model=KycInitiateOut)
@limiter.limit("3/minute")
async def kyc_initiate(
    request: Request,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Create (or re-create) a Didit verification session for this user."""
    if current_user.kyc_status == "approved":
        raise HTTPException(status_code=400, detail="Account is already verified.")

    if not DIDIT_API_KEY or not DIDIT_WORKFLOW_ID:
        raise HTTPException(
            status_code=503,
            detail="Identity verification is not configured on this server.",
        )

    async with httpx.AsyncClient(timeout=15) as client:
        resp = await client.post(
            f"{DIDIT_BASE_URL}/session/",
            headers={"x-api-key": DIDIT_API_KEY, "content-type": "application/json"},
            json={
                "workflow_id": DIDIT_WORKFLOW_ID,
                "vendor_data": str(current_user.id),
                "callback": os.environ.get(
                    "DIDIT_CALLBACK_URL",
                    "https://yourapp.example.com/auth/didit/webhook",
                ),
            },
        )

    if resp.status_code not in (200, 201):
        logger.error("Didit session create error %s: %s", resp.status_code, resp.text)
        try:
            didit_msg = resp.json().get("message") or resp.json().get("detail") or ""
        except Exception:
            didit_msg = ""
        raise HTTPException(
            status_code=502,
            detail=didit_msg or "Could not create verification session."
        )

    body = resp.json()
    session_id  = body.get("session_id", "")
    session_url = body.get("url") or body.get("verification_url", "")

    current_user.kyc_session_id = session_id
    current_user.kyc_status     = "pending"
    db.commit()

    return KycInitiateOut(
        session_id=session_id,
        verification_url=session_url,
        message="Please open the verification URL to complete your identity check.",
    )


@app.get("/auth/didit/callback")
async def didit_callback(
    verificationSessionId: str = "",
    status: str = "",
    db: Session = Depends(get_db),
):
    """
    Browser redirect target after Didit verification completes.
    Didit appends ?verificationSessionId=...&status=Approved|Declined|In+Review
    We look up the session, update the user, then return a close-tab page.
    """
    from fastapi.responses import HTMLResponse
    normalised = status.lower().strip()

    if verificationSessionId and normalised in ("approved", "declined"):
        user = db.query(User).filter(User.kyc_session_id == verificationSessionId).first()
        if user:
            if normalised == "approved":
                user.kyc_status = "approved"
            else:
                user.kyc_status = "declined"
            db.commit()
            await manager.send_to_user(user.id, {
                "type": "kyc_update",
                "data": {"kyc_status": user.kyc_status},
            })

    html = """<!DOCTYPE html><html><head><title>Forj — Verification</title>
<style>body{font-family:sans-serif;background:#36393F;color:#fff;
display:flex;align-items:center;justify-content:center;height:100vh;margin:0;}
.box{text-align:center;}</style></head><body><div class="box">
<h2>Verification complete</h2>
<p>You can close this tab and return to Forj.</p>
<script>setTimeout(()=>window.close(),2000);</script>
</div></body></html>"""
    return HTMLResponse(html)


@app.post("/auth/didit/webhook", status_code=204)
async def didit_webhook(request: Request, db: Session = Depends(get_db)):
    """
    Receives Didit verification results.
    Verify the HMAC-SHA256 signature when DIDIT_WEBHOOK_SECRET is set.
    """
    raw_body = await request.body()

    if DIDIT_WEBHOOK_SECRET:
        sig_header = request.headers.get("X-Signature", "")
        expected = hmac.new(
            DIDIT_WEBHOOK_SECRET.encode(),
            raw_body,
            hashlib.sha256,
        ).hexdigest()
        if not hmac.compare_digest(expected, sig_header.lower()):
            raise HTTPException(status_code=401, detail="Invalid webhook signature")

    try:
        payload = json.loads(raw_body)
    except json.JSONDecodeError:
        raise HTTPException(status_code=400, detail="Invalid JSON")

    # Didit sends vendor_data back as our user id string
    vendor_data = str(payload.get("vendor_data", ""))
    session_id  = payload.get("session_id", "")
    raw_status  = (payload.get("status") or "").lower()

    if not vendor_data.isdigit():
        return  # Not our session

    user = db.query(User).filter(User.id == int(vendor_data)).first()
    if not user:
        return

    if user.kyc_session_id and user.kyc_session_id != session_id:
        return  # Stale session from a previous attempt

    if raw_status == "approved":
        user.kyc_status = "approved"
        # Extract DOB from verification result (Didit field path may vary)
        verification = payload.get("verification") or payload.get("result") or {}
        document     = verification.get("document") or verification.get("id_document") or {}
        dob_raw      = (document.get("date_of_birth")
                        or document.get("dob")
                        or verification.get("date_of_birth")
                        or "")
        if dob_raw:
            user.date_of_birth = dob_raw[:10]  # normalise to YYYY-MM-DD
        db.commit()

    elif raw_status in ("declined", "expired"):
        user.kyc_status = "declined"
        db.commit()

    # Notify the user via WebSocket if they're connected
    await manager.send_to_user(user.id, {
        "type": "kyc_update",
        "data": {"kyc_status": user.kyc_status},
    })


@app.post("/auth/kyc/verify")
async def kyc_verify_direct(
    request: Request,
    front_image: UploadFile = File(...),
    back_image: Optional[UploadFile] = File(None),
    selfie_image: UploadFile = File(...),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """
    Native-client KYC: accept document photos + selfie, proxy to Didit
    standalone APIs, and update user status on success.
    """
    if current_user.kyc_status == "approved":
        return {"status": "approved", "message": "Already verified."}

    if not DIDIT_API_KEY:
        raise HTTPException(status_code=503, detail="KYC service not configured")

    front_bytes  = await front_image.read()
    back_bytes   = await back_image.read() if back_image else None
    selfie_bytes = await selfie_image.read()

    if not front_bytes or not selfie_bytes:
        raise HTTPException(status_code=422, detail="front_image and selfie_image are required")

    async with httpx.AsyncClient(timeout=60) as client:
        # --- ID verification ---
        id_files: dict = {
            "front_image": (front_image.filename or "front.jpg", front_bytes,
                            front_image.content_type or "image/jpeg"),
            "vendor_data": (None, str(current_user.id)),
        }
        if back_bytes:
            id_files["back_image"] = (back_image.filename or "back.jpg", back_bytes,
                                      back_image.content_type or "image/jpeg")

        id_resp = await client.post(
            f"{DIDIT_BASE_URL}/id-verification/",
            headers={"x-api-key": DIDIT_API_KEY},
            files=id_files,
        )

        if id_resp.status_code != 200:
            logger.error("Didit id-verification error %s: %s", id_resp.status_code, id_resp.text)
            return JSONResponse(
                status_code=502,
                content={"status": "error",
                         "message": f"ID verification service returned {id_resp.status_code}. Please try again later."},
            )

        id_data   = id_resp.json()
        id_check  = id_data.get("id_verification", {})
        id_status = (id_check.get("status") or "").lower()

        if id_status != "approved":
            warnings = id_check.get("warnings") or []
            reason = (warnings[0].get("short_description", "") if warnings
                      else id_check.get("decline_reason", ""))
            return JSONResponse(
                status_code=400,
                content={"status": "declined",
                         "message": f"ID check failed: {reason or 'Document could not be verified. Please try again with a clearer photo.'}"},
            )

        # --- Passive liveness check ---
        liveness_resp = await client.post(
            f"{DIDIT_BASE_URL}/passive-liveness/",
            headers={"x-api-key": DIDIT_API_KEY},
            files={
                "user_image": (selfie_image.filename or "selfie.jpg", selfie_bytes,
                               selfie_image.content_type or "image/jpeg"),
                "vendor_data": (None, str(current_user.id)),
            },
        )

        if liveness_resp.status_code != 200:
            logger.error("Didit passive-liveness error %s: %s", liveness_resp.status_code, liveness_resp.text)
            return JSONResponse(
                status_code=502,
                content={"status": "error",
                         "message": f"Liveness check service returned {liveness_resp.status_code}. Please try again later."},
            )

        liveness_data   = liveness_resp.json()
        liveness_status = (liveness_data.get("liveness", {}).get("status") or "").lower()

        if liveness_status != "approved":
            return JSONResponse(
                status_code=400,
                content={"status": "declined",
                         "message": "Liveness check failed. Please use a clear, well-lit photo of your face with no obstructions."},
            )

    # Both checks passed — update user record
    dob = id_check.get("date_of_birth") or ""
    current_user.kyc_status = "approved"
    if dob:
        current_user.date_of_birth = dob[:10]
    db.commit()

    age = _user_age(current_user)
    await manager.send_to_user(current_user.id, {
        "type": "kyc_update",
        "data": {"kyc_status": "approved", "age": age},
    })

    return {
        "status": "approved",
        "message": "Identity verified successfully! Welcome to Forj.",
        "age": age,
    }


@app.get("/auth/kyc/status")
def kyc_status_check(current_user: User = Depends(get_current_user)):
    """Lightweight endpoint for the client to poll while waiting for webhook."""
    return {"kyc_status": current_user.kyc_status}


@app.get("/friends", response_model=List[FriendOut])
def list_friends(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    rows = db.query(Friendship).filter(
        or_(
            Friendship.requester_id == current_user.id,
            Friendship.addressee_id == current_user.id,
        ),
        Friendship.status == "accepted",
    ).all()
    result = []
    for f in rows:
        other = f.addressee if f.requester_id == current_user.id else f.requester
        result.append(_friend_out(other))
    return result


@app.get("/friends/requests", response_model=List[FriendRequestOut])
def list_friend_requests(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    rows = db.query(Friendship).filter(
        Friendship.addressee_id == current_user.id,
        Friendship.status == "pending",
    ).all()
    return [_friend_req_out(f) for f in rows]


@app.post("/friends/request/{target_id}", status_code=status.HTTP_204_NO_CONTENT)
async def send_friend_request(
    target_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if target_id == current_user.id:
        raise HTTPException(status_code=400, detail="Cannot send a friend request to yourself")
    target = db.query(User).filter(User.id == target_id, User.is_active == True).first()
    if not target:
        raise HTTPException(status_code=404, detail="User not found")
    existing = db.query(Friendship).filter(
        or_(
            (Friendship.requester_id == current_user.id) & (Friendship.addressee_id == target_id),
            (Friendship.requester_id == target_id) & (Friendship.addressee_id == current_user.id),
        )
    ).first()
    if existing:
        if existing.status == "accepted":
            raise HTTPException(status_code=400, detail="Already friends")
        raise HTTPException(status_code=400, detail="Friend request already exists")
    db.add(Friendship(requester_id=current_user.id, addressee_id=target_id))
    db.commit()
    await manager.send_to_user(target_id, {
        "type": "friend_request",
        "data": {
            "from_id":           current_user.id,
            "from_username":     current_user.username,
            "from_display_name": current_user.display_name,
        },
    })


@app.post("/friends/accept/{requester_id}", status_code=status.HTTP_204_NO_CONTENT)
async def accept_friend_request(
    requester_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    f = db.query(Friendship).filter(
        Friendship.requester_id == requester_id,
        Friendship.addressee_id == current_user.id,
        Friendship.status == "pending",
    ).first()
    if not f:
        raise HTTPException(status_code=404, detail="No pending request from this user")
    f.status = "accepted"
    db.commit()
    await manager.send_to_user(requester_id, {
        "type": "friend_accepted",
        "data": {
            "by_id":           current_user.id,
            "by_username":     current_user.username,
            "by_display_name": current_user.display_name,
        },
    })


@app.delete("/friends/{other_id}", status_code=status.HTTP_204_NO_CONTENT)
def decline_or_remove_friend(
    other_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    f = db.query(Friendship).filter(
        or_(
            (Friendship.requester_id == current_user.id) & (Friendship.addressee_id == other_id),
            (Friendship.requester_id == other_id) & (Friendship.addressee_id == current_user.id),
        )
    ).first()
    if not f:
        raise HTTPException(status_code=404, detail="No friendship found")
    db.delete(f)
    db.commit()


@app.post("/guilds/{guild_id}/invite-user/{target_id}", status_code=status.HTTP_204_NO_CONTENT)
async def invite_user_to_guild(
    guild_id: int,
    target_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    if not (membership.role in ("owner", "admin") or _has_perm(membership, Perms.INVITE_MEMBERS)):
        raise HTTPException(status_code=403, detail="Insufficient permissions to invite users")
    target = db.query(User).filter(User.id == target_id, User.is_active == True).first()
    if not target:
        raise HTTPException(status_code=404, detail="User not found")
    guild = db.query(Guild).filter(Guild.id == guild_id).first()
    await manager.send_to_user(target_id, {
        "type": "guild_invite",
        "data": {
            "guild_id":          guild_id,
            "guild_name":        guild.name,
            "invite_code":       guild.invite_code,
            "inviter_username":  current_user.username,
        },
    })

# ── Guild routes ───────────────────────────────────────────────────────────────

@app.get("/guilds", response_model=List[GuildOut])
def list_guilds(current_user: User = Depends(get_current_user), db: Session = Depends(get_db)):
    memberships = db.query(GuildMember).filter(GuildMember.user_id == current_user.id).all()
    guild_ids   = [m.guild_id for m in memberships]
    guilds = db.query(Guild).filter(Guild.id.in_(guild_ids)).all()
    return [_guild_out(g) for g in guilds]


@app.post("/guilds", response_model=GuildOut, status_code=status.HTTP_201_CREATED)
def create_guild(
    data: GuildCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    invite_code = secrets.token_urlsafe(8)
    guild = Guild(name=data.name, owner_id=current_user.id, invite_code=invite_code)
    db.add(guild)
    db.flush()

    db.add(GuildMember(guild_id=guild.id, user_id=current_user.id, role="owner"))
    db.add(Channel(guild_id=guild.id, name="general", channel_type="text"))
    db.commit()
    db.refresh(guild)
    return _guild_out(guild)


@app.post("/guilds/join", response_model=GuildOut)
def join_guild(
    data: GuildJoin,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    guild = db.query(Guild).filter(Guild.invite_code == data.invite_code).first()
    if not guild:
        raise HTTPException(status_code=404, detail="Invalid invite code")

    already = db.query(GuildMember).filter(
        GuildMember.guild_id == guild.id,
        GuildMember.user_id  == current_user.id,
    ).first()
    if already:
        raise HTTPException(status_code=400, detail="Already a member")

    # Check if banned
    ban = db.query(Ban).filter(Ban.guild_id == guild.id, Ban.user_id == current_user.id).first()
    if ban:
        raise HTTPException(status_code=403, detail="You are banned from this server")

    db.add(GuildMember(guild_id=guild.id, user_id=current_user.id, role="member"))
    db.commit()
    return _guild_out(guild)


@app.delete("/guilds/{guild_id}/leave", status_code=status.HTTP_204_NO_CONTENT)
def leave_guild(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    guild = db.query(Guild).filter(Guild.id == guild_id).first()
    if guild and guild.owner_id == current_user.id:
        raise HTTPException(status_code=400, detail="Owner cannot leave; transfer ownership or delete the guild")
    db.delete(membership)
    db.commit()


# ── Guild management ──────────────────────────────────────────────────────────

@app.patch("/guilds/{guild_id}", response_model=GuildOut)
def update_guild(
    guild_id: int,
    data: GuildUpdate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.MANAGE_GUILD)
    guild = db.query(Guild).filter(Guild.id == guild_id).first()
    guild.name = data.name
    db.commit()
    db.refresh(guild)
    return _guild_out(guild)


@app.delete("/guilds/{guild_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_guild(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    guild = db.query(Guild).filter(Guild.id == guild_id).first()
    if not guild:
        raise HTTPException(status_code=404, detail="Guild not found")
    if guild.owner_id != current_user.id:
        raise HTTPException(status_code=403, detail="Only the owner can delete this server")
    db.delete(guild)
    db.commit()


@app.post("/guilds/{guild_id}/icon", status_code=status.HTTP_204_NO_CONTENT)
async def upload_guild_icon(
    guild_id: int,
    file: UploadFile = File(...),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.MANAGE_GUILD)
    data = await file.read(512 * 1024 + 1)
    if len(data) > 512 * 1024:
        raise HTTPException(status_code=413, detail="Icon too large (max 512 KB)")
    guild = db.query(Guild).filter(Guild.id == guild_id).first()
    guild.icon      = data
    guild.icon_type = file.content_type or "image/png"
    db.commit()


@app.get("/guilds/{guild_id}/icon")
def get_guild_icon(
    guild_id: int,
    db: Session = Depends(get_db),
):
    guild = db.query(Guild).filter(Guild.id == guild_id).first()
    if not guild or not guild.icon:
        raise HTTPException(status_code=404, detail="No icon")
    return Response(content=guild.icon, media_type=guild.icon_type or "image/png")


# ── Role management ───────────────────────────────────────────────────────────

@app.get("/guilds/{guild_id}/roles", response_model=List[RoleOut])
def list_roles(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    _require_member(db, guild_id, current_user.id)
    roles = db.query(Role).filter(Role.guild_id == guild_id).order_by(Role.position).all()
    return [_role_out(r) for r in roles]


@app.post("/guilds/{guild_id}/roles", response_model=RoleOut, status_code=status.HTTP_201_CREATED)
def create_role(
    guild_id: int,
    data: RoleCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.MANAGE_ROLES)
    role = Role(
        guild_id=guild_id,
        name=data.name,
        color=data.color,
        permissions=data.permissions,
        position=data.position,
        hoist=data.hoist,
    )
    db.add(role)
    db.commit()
    db.refresh(role)
    return _role_out(role)


@app.patch("/guilds/{guild_id}/roles/{role_id}", response_model=RoleOut)
def update_role(
    guild_id: int,
    role_id: int,
    data: RoleUpdate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.MANAGE_ROLES)
    role = db.query(Role).filter(Role.id == role_id, Role.guild_id == guild_id).first()
    if not role:
        raise HTTPException(status_code=404, detail="Role not found")
    if data.name        is not None: role.name        = data.name
    if data.color       is not None: role.color       = data.color
    if data.permissions is not None: role.permissions = data.permissions
    if data.position    is not None: role.position    = data.position
    if data.hoist       is not None: role.hoist       = data.hoist
    db.commit()
    db.refresh(role)
    return _role_out(role)


@app.delete("/guilds/{guild_id}/roles/{role_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_role(
    guild_id: int,
    role_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.MANAGE_ROLES)
    role = db.query(Role).filter(Role.id == role_id, Role.guild_id == guild_id).first()
    if not role:
        raise HTTPException(status_code=404, detail="Role not found")
    db.delete(role)
    db.commit()


@app.post("/guilds/{guild_id}/members/{target_id}/roles/{role_id}",
          status_code=status.HTTP_204_NO_CONTENT)
def assign_role(
    guild_id: int, target_id: int, role_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.MANAGE_ROLES)
    target_member = _require_member(db, guild_id, target_id)
    role = db.query(Role).filter(Role.id == role_id, Role.guild_id == guild_id).first()
    if not role:
        raise HTTPException(status_code=404, detail="Role not found")
    existing = db.query(MemberRole).filter(
        MemberRole.member_id == target_member.id,
        MemberRole.role_id   == role_id,
    ).first()
    if not existing:
        db.add(MemberRole(member_id=target_member.id, role_id=role_id))
        db.commit()


@app.delete("/guilds/{guild_id}/members/{target_id}/roles/{role_id}",
            status_code=status.HTTP_204_NO_CONTENT)
def remove_role(
    guild_id: int, target_id: int, role_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.MANAGE_ROLES)
    target_member = _require_member(db, guild_id, target_id)
    mr = db.query(MemberRole).filter(
        MemberRole.member_id == target_member.id,
        MemberRole.role_id   == role_id,
    ).first()
    if mr:
        db.delete(mr)
        db.commit()


# ── Member moderation ─────────────────────────────────────────────────────────

@app.post("/guilds/{guild_id}/members/{target_id}/kick",
          status_code=status.HTTP_204_NO_CONTENT)
def kick_member(
    guild_id: int, target_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.KICK_MEMBERS)
    target = _require_member(db, guild_id, target_id)
    if target.role == "owner":
        raise HTTPException(status_code=403, detail="Cannot kick the owner")
    db.delete(target)
    db.commit()


@app.post("/guilds/{guild_id}/members/{target_id}/ban",
          status_code=status.HTTP_204_NO_CONTENT)
def ban_member(
    guild_id: int, target_id: int,
    data: BanCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.BAN_MEMBERS)
    target = _require_member(db, guild_id, target_id)
    if target.role == "owner":
        raise HTTPException(status_code=403, detail="Cannot ban the owner")
    # Remove from guild first
    db.delete(target)
    # Add ban record (upsert)
    existing = db.query(Ban).filter(Ban.guild_id == guild_id, Ban.user_id == target_id).first()
    if not existing:
        db.add(Ban(guild_id=guild_id, user_id=target_id,
                   reason=data.reason, banned_by=current_user.id))
    db.commit()


@app.get("/guilds/{guild_id}/bans", response_model=List[BanOut])
def list_bans(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.BAN_MEMBERS)
    bans = db.query(Ban).filter(Ban.guild_id == guild_id).all()
    return [_ban_out(b) for b in bans]


@app.delete("/guilds/{guild_id}/bans/{target_id}", status_code=status.HTTP_204_NO_CONTENT)
def unban_member(
    guild_id: int, target_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    _require_perm(membership, Perms.BAN_MEMBERS)
    ban = db.query(Ban).filter(Ban.guild_id == guild_id, Ban.user_id == target_id).first()
    if ban:
        db.delete(ban)
        db.commit()


# ── Per-server profile ────────────────────────────────────────────────────────

@app.get("/guilds/{guild_id}/server-profile", response_model=ServerProfileOut)
def get_server_profile(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    _require_member(db, guild_id, current_user.id)
    sp = db.query(ServerProfile).filter(
        ServerProfile.guild_id == guild_id,
        ServerProfile.user_id  == current_user.id,
    ).first()
    if not sp:
        return ServerProfileOut(guild_id=guild_id, user_id=current_user.id,
                                nickname=None, has_avatar=False)
    return _server_profile_out(sp)


@app.put("/guilds/{guild_id}/server-profile", response_model=ServerProfileOut)
def update_server_profile(
    guild_id: int,
    data: ServerProfileUpdate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    _require_member(db, guild_id, current_user.id)
    sp = db.query(ServerProfile).filter(
        ServerProfile.guild_id == guild_id,
        ServerProfile.user_id  == current_user.id,
    ).first()
    if not sp:
        sp = ServerProfile(guild_id=guild_id, user_id=current_user.id)
        db.add(sp)
    sp.nickname = data.nickname
    db.commit()
    db.refresh(sp)
    return _server_profile_out(sp)


@app.post("/guilds/{guild_id}/server-profile/avatar", status_code=status.HTTP_204_NO_CONTENT)
async def upload_server_avatar(
    guild_id: int,
    file: UploadFile = File(...),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    _require_member(db, guild_id, current_user.id)
    data = await file.read(512 * 1024 + 1)
    if len(data) > 512 * 1024:
        raise HTTPException(status_code=413, detail="Avatar too large (max 512 KB)")
    sp = db.query(ServerProfile).filter(
        ServerProfile.guild_id == guild_id,
        ServerProfile.user_id  == current_user.id,
    ).first()
    if not sp:
        sp = ServerProfile(guild_id=guild_id, user_id=current_user.id)
        db.add(sp)
    sp.avatar      = data
    sp.avatar_type = file.content_type or "image/png"
    db.commit()


@app.get("/guilds/{guild_id}/members/{target_id}/avatar")
def get_member_avatar(
    guild_id: int, target_id: int,
    db: Session = Depends(get_db),
):
    """Return server-profile avatar if set, else global avatar, else 404."""
    sp = db.query(ServerProfile).filter(
        ServerProfile.guild_id == guild_id,
        ServerProfile.user_id  == target_id,
    ).first()
    if sp and sp.avatar:
        return Response(content=sp.avatar, media_type=sp.avatar_type or "image/png")
    user = db.query(User).filter(User.id == target_id).first()
    if user and user.avatar:
        return Response(content=user.avatar, media_type=user.avatar_type or "image/png")
    raise HTTPException(status_code=404, detail="No avatar")


@app.get("/guilds/{guild_id}/channels", response_model=List[ChannelOut])
def list_channels(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    _require_member(db, guild_id, current_user.id)
    return db.query(Channel).filter(Channel.guild_id == guild_id).order_by(Channel.id).all()


@app.post("/guilds/{guild_id}/channels", response_model=ChannelOut, status_code=status.HTTP_201_CREATED)
def create_channel(
    guild_id: int,
    data: ChannelCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    membership = _require_member(db, guild_id, current_user.id)
    if membership.role not in ("owner", "admin"):
        raise HTTPException(status_code=403, detail="Insufficient permissions")

    channel = Channel(guild_id=guild_id, name=data.name, channel_type=data.channel_type)
    db.add(channel)
    db.commit()
    db.refresh(channel)
    return channel


# ── Voice state routes ─────────────────────────────────────────────────────────

class VoiceParticipantOut(BaseModel):
    user_id: int
    username: str


@app.get("/channels/{channel_id}/voice", response_model=List[VoiceParticipantOut])
def get_voice_state(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    _require_member(db, channel.guild_id, current_user.id)
    participants = _voice_state.get(channel_id, {})
    return [{"user_id": uid, "username": uname} for uid, uname in participants.items()]


@app.post("/channels/{channel_id}/voice/join", status_code=status.HTTP_204_NO_CONTENT)
async def join_voice(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    if channel.channel_type != "voice":
        raise HTTPException(status_code=400, detail="Not a voice channel")
    _require_member(db, channel.guild_id, current_user.id)

    # Leave any other voice channel in this guild first
    for ch_id, participants in _voice_state.items():
        if current_user.id in participants and ch_id != channel_id:
            participants.pop(current_user.id, None)
            await manager.broadcast_to_guild(channel.guild_id, {
                "type": "voice_leave",
                "data": {
                    "channel_id": ch_id,
                    "user_id": current_user.id,
                    "username": current_user.username,
                },
            })

    _voice_state.setdefault(channel_id, {})[current_user.id] = current_user.username
    await manager.broadcast_to_guild(channel.guild_id, {
        "type": "voice_join",
        "data": {
            "channel_id": channel_id,
            "user_id": current_user.id,
            "username": current_user.username,
        },
    })


@app.post("/channels/{channel_id}/voice/leave", status_code=status.HTTP_204_NO_CONTENT)
async def leave_voice(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    _require_member(db, channel.guild_id, current_user.id)

    if current_user.id in _voice_state.get(channel_id, {}):
        _voice_state[channel_id].pop(current_user.id, None)
        await manager.broadcast_to_guild(channel.guild_id, {
            "type": "voice_leave",
            "data": {
                "channel_id": channel_id,
                "user_id": current_user.id,
                "username": current_user.username,
            },
        })


@app.get("/guilds/{guild_id}/members", response_model=List[MemberOut])
def list_members(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    _require_member(db, guild_id, current_user.id)
    memberships  = db.query(GuildMember).filter(GuildMember.guild_id == guild_id).all()
    online_users = manager.online_in_guild(guild_id)
    return [_member_out(m, online_users, db) for m in memberships]


# ── Message routes ─────────────────────────────────────────────────────────────

@app.get("/channels/{channel_id}/messages", response_model=List[MessageOut])
def list_messages(
    channel_id: int,
    limit: int = 50,
    before_id: Optional[int] = None,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    _require_member(db, channel.guild_id, current_user.id)

    q = db.query(Message).filter(Message.channel_id == channel_id)
    if before_id is not None:
        q = q.filter(Message.id < before_id)
    rows = q.order_by(Message.id.desc()).limit(min(limit, 100)).all()
    rows.reverse()
    return [_msg_out(m) for m in rows]


@app.post("/channels/{channel_id}/messages", response_model=MessageOut, status_code=status.HTTP_201_CREATED)
async def send_message(
    channel_id: int,
    data: MessageCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    _require_member(db, channel.guild_id, current_user.id)

    # Validate reply target belongs to same channel
    reply_to_id: Optional[int] = None
    if data.reply_to_id:
        rt = db.query(Message).filter(
            Message.id == data.reply_to_id,
            Message.channel_id == channel_id,
        ).first()
        if rt:
            reply_to_id = rt.id

    msg = Message(channel_id=channel_id, author_id=current_user.id,
                  content=data.content, reply_to_id=reply_to_id)
    db.add(msg)
    db.commit()
    db.refresh(msg)

    out = _msg_out(msg)
    await manager.broadcast_to_guild(channel.guild_id, {
        "type": "new_message",
        "data": {
            "id":                out.id,
            "channel_id":        out.channel_id,
            "author_id":         out.author_id,
            "author_username":   out.author_username,
            "author_has_avatar": out.author_has_avatar,
            "author_is_bot":     out.author_is_bot,
            "content":           out.content,
            "reply_to_id":       out.reply_to_id,
            "reply_to_author":   out.reply_to_author,
            "reply_to_content":  out.reply_to_content,
            "created_at":        out.created_at.isoformat(),
        },
    })

    # ── @mention notifications ────────────────────────────────────────────────────
    mentioned_names = set(re.findall(r'@([a-zA-Z0-9_]+)', data.content))
    for uname in mentioned_names:
        if uname.lower() == current_user.username.lower():
            continue
        mentioned_user = db.query(User).filter(
            User.username.ilike(uname), User.is_active == True
        ).first()
        if not mentioned_user:
            continue
        is_member = db.query(GuildMember).filter(
            GuildMember.guild_id == channel.guild_id,
            GuildMember.user_id  == mentioned_user.id,
        ).first()
        if is_member:
            await manager.send_to_user(mentioned_user.id, {
                "type": "mention",
                "data": {
                    "guild_id":        channel.guild_id,
                    "channel_id":      channel_id,
                    "channel_name":    channel.name,
                    "author_username": current_user.username,
                    "content":         data.content[:200],
                    "message_id":      msg.id,
                },
            })

    return out


# ── User profile routes ────────────────────────────────────────────────────────

@app.get("/users/search", response_model=List[UserProfileOut])
def search_users(
    q: str,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if len(q) < 2:
        raise HTTPException(status_code=400, detail="Query too short")
    rows = (db.query(User)
              .filter(User.username.ilike(f"%{q}%"), User.is_active == True, User.is_bot == False)
              .limit(20).all())
    return [_profile_out(u) for u in rows]


@app.get("/users/{user_id}/profile", response_model=UserProfileOut)
def get_user_profile(
    user_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    user = db.query(User).filter(User.id == user_id, User.is_active == True).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    return _profile_out(user)


@app.get("/users/{user_id}/avatar")
def get_avatar(user_id: int, db: Session = Depends(get_db)):
    user = db.query(User).filter(User.id == user_id).first()
    if not user or not user.avatar:
        raise HTTPException(status_code=404, detail="No avatar")
    return Response(content=user.avatar, media_type=user.avatar_type or "image/png")


@app.patch("/users/me", response_model=UserOut)
def update_profile(
    data: UserUpdate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if data.display_name is not None:
        current_user.display_name = data.display_name.strip() or None
    if data.bio is not None:
        current_user.bio = data.bio.strip() or None
    db.commit()
    db.refresh(current_user)
    return _user_out(current_user)


@app.post("/users/me/avatar", status_code=status.HTTP_204_NO_CONTENT)
async def upload_avatar(
    file: UploadFile = File(...),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if file.content_type not in ("image/png", "image/jpeg", "image/gif", "image/webp"):
        raise HTTPException(status_code=400, detail="Unsupported image type")
    data = await file.read(512 * 1024)   # max 512 KB
    if len(data) == 512 * 1024:
        raise HTTPException(status_code=413, detail="Avatar must be ≤ 512 KB")
    current_user.avatar      = data
    current_user.avatar_type = file.content_type
    db.commit()


@app.post("/users/me/password", status_code=status.HTTP_204_NO_CONTENT)
def change_password(
    data: PasswordChange,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if not verify_password(data.current_password, current_user.hashed_password):
        raise HTTPException(status_code=400, detail="Current password is incorrect")
    current_user.hashed_password = hash_password(data.new_password)
    # Increment token_version so all existing sessions are invalidated
    current_user.token_version = (current_user.token_version or 0) + 1
    db.commit()


@app.post("/users/me/status", status_code=status.HTTP_204_NO_CONTENT)
async def set_status(
    data: StatusUpdate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    current_user.status = data.status
    db.commit()
    # Broadcast the visible status to all guild-mates so their member lists update
    visible = "offline" if data.status == "invisible" else data.status
    memberships = db.query(GuildMember).filter(GuildMember.user_id == current_user.id).all()
    for m in memberships:
        await manager.broadcast_to_guild(
            m.guild_id,
            {"type": "user_status", "data": {
                "user_id":  current_user.id,
                "username": current_user.username,
                "status":   visible,
            }},
        )


# ── Direct Message routes ─────────────────────────────────────────────────────

@app.get("/dms", response_model=List[DmConversationOut])
def list_dms(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    convs = db.query(DirectConversation).filter(
        (DirectConversation.user1_id == current_user.id) |
        (DirectConversation.user2_id == current_user.id)
    ).all()
    return [_dm_conv_out(c, current_user.id) for c in convs]


@app.post("/dms", response_model=DmConversationOut)
def open_dm(
    data: DmCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if data.user_id == current_user.id:
        raise HTTPException(status_code=400, detail="Cannot DM yourself")
    other = db.query(User).filter(User.id == data.user_id, User.is_active == True).first()
    if not other:
        raise HTTPException(status_code=404, detail="User not found")

    uid1, uid2 = sorted([current_user.id, data.user_id])
    conv = db.query(DirectConversation).filter(
        DirectConversation.user1_id == uid1,
        DirectConversation.user2_id == uid2,
    ).first()
    if not conv:
        conv = DirectConversation(user1_id=uid1, user2_id=uid2)
        db.add(conv)
        db.commit()
        db.refresh(conv)
    return _dm_conv_out(conv, current_user.id)


@app.get("/dms/{dm_id}/messages", response_model=List[DmMessageOut])
def get_dm_messages(
    dm_id: int,
    limit: int = 50,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    conv = _require_dm_access(db, dm_id, current_user.id)
    rows = (db.query(DirectMessage)
              .filter(DirectMessage.conversation_id == conv.id)
              .order_by(DirectMessage.id.desc())
              .limit(min(limit, 100)).all())
    rows.reverse()
    return [_dm_msg_out(m) for m in rows]


@app.post("/dms/{dm_id}/messages", response_model=DmMessageOut, status_code=status.HTTP_201_CREATED)
async def send_dm(
    dm_id: int,
    data: DmMessageCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    conv = _require_dm_access(db, dm_id, current_user.id)

    # ── Frozen conversation check ─────────────────────────────────────────────
    frozen = db.query(FrozenConversation).filter(
        FrozenConversation.conversation_id == conv.id
    ).first()
    if frozen:
        raise HTTPException(
            status_code=403,
            detail="This conversation has been frozen by Forj Safety. Contact Forj Support.",
        )

    # Validate reply target belongs to same conversation
    reply_to_id: Optional[int] = None
    if data.reply_to_id:
        rt = db.query(DirectMessage).filter(
            DirectMessage.id == data.reply_to_id,
            DirectMessage.conversation_id == conv.id,
        ).first()
        if rt:
            reply_to_id = rt.id

    msg = DirectMessage(conversation_id=conv.id, author_id=current_user.id,
                        content=data.content, reply_to_id=reply_to_id)
    db.add(msg)
    db.commit()
    db.refresh(msg)

    out = _dm_msg_out(msg)
    other_id = conv.user2_id if conv.user1_id == current_user.id else conv.user1_id

    # ── Child safety / grooming detection ────────────────────────────────────
    # Only scan if there is a verified adult messaging a verified minor (or vice versa)
    other_user   = db.query(User).filter(User.id == other_id).first()
    sender_minor = _is_minor(current_user)
    recip_minor  = _is_minor(other_user) if other_user else None

    if (
        sender_minor is not None
        and recip_minor is not None
        and sender_minor != recip_minor   # one adult, one minor
        and current_user.kyc_status == "approved"
        and (other_user and other_user.kyc_status == "approved")
    ):
        result = analyze_message(data.content)
        if result.flagged:
            flag = SafetyFlag(
                conversation_id=conv.id,
                message_content=data.content[:500],
                patterns=",".join(result.patterns),
                severity_score=result.severity,
            )
            db.add(flag)
            db.commit()

            # Sum total severity for this conversation
            total_severity = db.query(SafetyFlag).filter(
                SafetyFlag.conversation_id == conv.id
            ).all()
            total = sum(f.severity_score for f in total_severity)

            if total >= FREEZE_THRESHOLD:
                reason = build_reason(
                    [p for f in total_severity for p in f.patterns.split(",") if p]
                )
                # Freeze the conversation
                fc = FrozenConversation(conversation_id=conv.id, reason=reason)
                db.add(fc)

                # System message visible in chat history
                system_msg = DirectMessage(
                    conversation_id=conv.id,
                    author_id=_system_user_id,
                    content=(
                        "Forj Administration: This conversation has been frozen for safety reasons. "
                        "Contact Forj Support for more information."
                    ),
                )
                db.add(system_msg)
                db.commit()

                # Identify adult vs minor
                if sender_minor:
                    minor_user, adult_user = current_user, other_user
                else:
                    adult_user, minor_user = current_user, other_user

                adult_notice = (
                    f"Forj Administration: Please be advised that your ability to talk to "
                    f"{minor_user.username} has been blocked. "
                    f"Reason: {reason}. "
                    f"Please contact Forj Support for more details."
                )
                minor_notice = (
                    f"Forj Administration: For your safety, Forj has blocked your interactions "
                    f"between you and {adult_user.username} for these reasons: {reason}. "
                    f"Please immediately show this message to your parents."
                )

                await manager.send_to_user(adult_user.id, {
                    "type": "safety_freeze",
                    "data": {
                        "conversation_id": conv.id,
                        "message": adult_notice,
                    },
                })
                await manager.send_to_user(minor_user.id, {
                    "type": "safety_freeze",
                    "data": {
                        "conversation_id": conv.id,
                        "message": minor_notice,
                    },
                })

    await manager.send_to_user(other_id, {
        "type": "dm_message",
        "data": {
            "id":              out.id,
            "conversation_id": out.conversation_id,
            "author_id":       out.author_id,
            "author_username": out.author_username,
            "author_has_avatar": out.author_has_avatar,
            "author_is_bot":   out.author_is_bot,
            "content":         out.content,
            "reply_to_id":     out.reply_to_id,
            "reply_to_author": out.reply_to_author,
            "reply_to_content":out.reply_to_content,
            "created_at":      out.created_at.isoformat(),
        },
    })
    return out


# ── Bot routes ────────────────────────────────────────────────────────────────

@app.get("/bots", response_model=List[BotOut])
def list_bots(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    bots = db.query(Bot).filter(Bot.owner_id == current_user.id).all()
    return [_bot_out(b) for b in bots]


@app.post("/bots", response_model=BotCreatedOut, status_code=status.HTTP_201_CREATED)
def create_bot(
    data: BotCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    bot_username = f"bot_{data.name.lower().replace(' ', '_')}_{secrets.token_hex(3)}"
    bot_user = User(
        username=bot_username,
        email=f"{bot_username}@bots.forj.internal",
        hashed_password="",
        display_name=data.name,
        is_bot=True,
    )
    db.add(bot_user)
    db.flush()

    bot = Bot(user_id=bot_user.id, owner_id=current_user.id)
    db.add(bot)
    db.commit()
    db.refresh(bot)

    token = create_bot_token(bot_user.id)
    return BotCreatedOut(
        id=bot.id, user_id=bot_user.id,
        username=bot_user.username, name=data.name, token=token,
    )


@app.delete("/bots/{bot_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_bot(
    bot_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    bot = db.query(Bot).filter(Bot.id == bot_id, Bot.owner_id == current_user.id).first()
    if not bot:
        raise HTTPException(status_code=404, detail="Bot not found")
    bot_user = db.query(User).filter(User.id == bot.user_id).first()
    db.delete(bot)
    if bot_user:
        db.delete(bot_user)
    db.commit()


@app.post("/bots/{bot_id}/reset-token", response_model=BotCreatedOut)
def reset_bot_token(
    bot_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    bot = db.query(Bot).filter(Bot.id == bot_id, Bot.owner_id == current_user.id).first()
    if not bot:
        raise HTTPException(status_code=404, detail="Bot not found")
    token = create_bot_token(bot.user_id)
    return BotCreatedOut(
        id=bot.id, user_id=bot.user_id,
        username=bot.bot_user.username, name=bot.bot_user.display_name or "", token=token,
    )


# ── Slash command routes ──────────────────────────────────────────────────────

@app.get("/guilds/{guild_id}/commands", response_model=List[CommandOut])
def list_commands(
    guild_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    _require_member(db, guild_id, current_user.id)
    cmds = db.query(SlashCommand).filter(SlashCommand.guild_id == guild_id).all()
    return [_cmd_out(c) for c in cmds]


@app.post("/guilds/{guild_id}/commands", response_model=CommandOut, status_code=status.HTTP_201_CREATED)
def register_command(
    guild_id: int,
    data: CommandCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    if not current_user.is_bot:
        raise HTTPException(status_code=403, detail="Only bots can register commands")
    bot = db.query(Bot).filter(Bot.user_id == current_user.id).first()
    if not bot:
        raise HTTPException(status_code=403, detail="Bot record not found")
    _require_member(db, guild_id, current_user.id)

    existing = db.query(SlashCommand).filter(
        SlashCommand.guild_id == guild_id, SlashCommand.name == data.name
    ).first()
    if existing:
        raise HTTPException(status_code=409, detail="Command name already registered in this guild")

    cmd = SlashCommand(guild_id=guild_id, bot_id=bot.id,
                       name=data.name, description=data.description)
    db.add(cmd)
    db.commit()
    db.refresh(cmd)
    return _cmd_out(cmd)


@app.delete("/guilds/{guild_id}/commands/{cmd_id}", status_code=status.HTTP_204_NO_CONTENT)
def unregister_command(
    guild_id: int,
    cmd_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    cmd = db.query(SlashCommand).filter(
        SlashCommand.id == cmd_id, SlashCommand.guild_id == guild_id
    ).first()
    if not cmd:
        raise HTTPException(status_code=404, detail="Command not found")
    is_bot_owner = current_user.is_bot and cmd.bot.user_id == current_user.id
    mem = db.query(GuildMember).filter(
        GuildMember.guild_id == guild_id, GuildMember.user_id == current_user.id
    ).first()
    is_admin = mem and mem.role in ("owner", "admin")
    if not is_bot_owner and not is_admin:
        raise HTTPException(status_code=403, detail="Insufficient permissions")
    db.delete(cmd)
    db.commit()


@app.post("/channels/{channel_id}/invoke", response_model=MessageOut, status_code=status.HTTP_201_CREATED)
async def invoke_command(
    channel_id: int,
    data: CommandInvoke,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    _require_member(db, channel.guild_id, current_user.id)

    cmd = db.query(SlashCommand).filter(
        SlashCommand.guild_id == channel.guild_id,
        SlashCommand.name == data.name,
    ).first()

    # Store the invocation as a regular message (visible in chat)
    content = f"/{data.name} {data.args}".rstrip()
    msg = Message(channel_id=channel_id, author_id=current_user.id, content=content)
    db.add(msg)
    db.commit()
    db.refresh(msg)
    out = _msg_out(msg)

    await manager.broadcast_to_guild(channel.guild_id, {
        "type": "new_message",
        "data": {
            "id":              out.id,
            "channel_id":      out.channel_id,
            "author_id":       out.author_id,
            "author_username": out.author_username,
            "content":         out.content,
            "created_at":      out.created_at.isoformat(),
        },
    })

    if cmd:
        bot_user_id = cmd.bot.user_id
        if manager.is_online(bot_user_id):
            await manager.send_to_user(bot_user_id, {
                "type": "interaction",
                "data": {
                    "interaction_id": str(uuid.uuid4()),
                    "command":    cmd.name,
                    "args":       data.args,
                    "channel_id": channel_id,
                    "guild_id":   channel.guild_id,
                    "user_id":    current_user.id,
                    "username":   current_user.username,
                },
            })
        # else: bot offline — invocation still stored in chat, bot just won't respond

    return out


# ── Webhook routes ────────────────────────────────────────────────────────────

@app.get("/channels/{channel_id}/webhooks", response_model=List[WebhookOut])
def list_webhooks(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    mem = _require_member(db, channel.guild_id, current_user.id)
    if mem.role not in ("owner", "admin"):
        raise HTTPException(status_code=403, detail="Admin only")
    hooks = db.query(Webhook).filter(Webhook.channel_id == channel_id).all()
    return [_hook_out(h) for h in hooks]


@app.post("/channels/{channel_id}/webhooks", response_model=WebhookOut, status_code=status.HTTP_201_CREATED)
def create_webhook(
    channel_id: int,
    data: WebhookCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    channel = _get_channel_or_404(db, channel_id)
    mem = _require_member(db, channel.guild_id, current_user.id)
    if mem.role not in ("owner", "admin"):
        raise HTTPException(status_code=403, detail="Admin only")
    hook = Webhook(
        channel_id=channel_id, name=data.name,
        token=secrets.token_urlsafe(32), created_by=current_user.id,
    )
    db.add(hook)
    db.commit()
    db.refresh(hook)
    return _hook_out(hook)


@app.delete("/webhooks/{webhook_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_webhook(
    webhook_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    hook = db.query(Webhook).filter(Webhook.id == webhook_id).first()
    if not hook:
        raise HTTPException(status_code=404, detail="Webhook not found")
    channel = _get_channel_or_404(db, hook.channel_id)
    mem = _require_member(db, channel.guild_id, current_user.id)
    if mem.role not in ("owner", "admin"):
        raise HTTPException(status_code=403, detail="Admin only")
    db.delete(hook)
    db.commit()


@app.post("/webhooks/{webhook_id}/{webhook_token}", response_model=MessageOut)
async def post_via_webhook(
    webhook_id: int,
    webhook_token: str,
    data: WebhookPost,
    db: Session = Depends(get_db),
):
    hook = db.query(Webhook).filter(
        Webhook.id == webhook_id, Webhook.token == webhook_token
    ).first()
    if not hook:
        raise HTTPException(status_code=404, detail="Invalid webhook")

    # Find or create a system user to own the message (webhook bot user)
    bot_username = f"webhook_{hook.id}"
    wbot = db.query(User).filter(User.username == bot_username).first()
    if not wbot:
        wbot = User(
            username=bot_username,
            email=f"{bot_username}@webhooks.forj.internal",
            hashed_password="",
            display_name=data.username or hook.name,
            is_bot=True,
        )
        db.add(wbot)
        db.flush()

    display = data.username or hook.name
    wbot.display_name = display

    msg = Message(
        channel_id=hook.channel_id,
        author_id=wbot.id,
        content=data.content,
        webhook_name=display,
    )
    db.add(msg)
    db.commit()
    db.refresh(msg)
    out = _msg_out(msg)

    channel = _get_channel_or_404(db, hook.channel_id)
    await manager.broadcast_to_guild(channel.guild_id, {
        "type": "new_message",
        "data": {
            "id":              out.id,
            "channel_id":      out.channel_id,
            "author_id":       out.author_id,
            "author_username": out.author_username,
            "content":         out.content,
            "created_at":      out.created_at.isoformat(),
        },
    })
    return out


# ── WebSocket endpoint ─────────────────────────────────────────────────────────

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket, db: Session = Depends(get_db)):
    await websocket.accept()
    user_id: Optional[int] = None
    username: str = ""

    try:
        # ── 1. Authentication handshake (10-second window) ────────────────────
        raw      = await asyncio.wait_for(websocket.receive_text(), timeout=10.0)
        auth_msg = json.loads(raw)

        if auth_msg.get("type") != "authenticate":
            await websocket.close(code=4001, reason="Expected authenticate message")
            return

        user_id = verify_token(auth_msg.get("token", ""))
        if user_id is None:
            await websocket.close(code=4001, reason="Invalid token")
            return

        user = db.query(User).filter(User.id == user_id, User.is_active == True).first()
        if user is None:
            await websocket.close(code=4001, reason="User not found")
            return

        username = user.username
        manager.register(user_id, websocket)

        # ── 2. Subscribe to all guilds the user belongs to ────────────────────
        memberships = db.query(GuildMember).filter(GuildMember.user_id == user_id).all()
        guild_ids   = [m.guild_id for m in memberships]
        for gid in guild_ids:
            manager.subscribe(user_id, gid)

        # Announce presence (skip for invisible users — they appear offline to others)
        for gid in guild_ids:
            if user.status != "invisible":
                await manager.broadcast_to_guild(
                    gid,
                    {"type": "user_online", "data": {"user_id": user_id, "username": username}},
                    exclude=user_id,
                )

        await websocket.send_text(json.dumps({
            "type": "authenticated",
            "data": {"user_id": user_id, "username": username, "guild_ids": guild_ids},
        }))

        # ── 3. Main message loop ───────────────────────────────────────────────
        while True:
            raw = await websocket.receive_text()
            try:
                msg      = json.loads(raw)
                msg_type = msg.get("type")
            except (json.JSONDecodeError, AttributeError):
                continue

            if msg_type == "subscribe":
                # Client joined a new guild and wants to subscribe
                for gid in msg.get("guild_ids", []):
                    if not isinstance(gid, int):
                        continue
                    m = db.query(GuildMember).filter(
                        GuildMember.guild_id == gid,
                        GuildMember.user_id  == user_id,
                    ).first()
                    if m:
                        manager.subscribe(user_id, gid)
            elif msg_type == "ping":
                await websocket.send_text(json.dumps({"type": "pong"}))
            elif msg_type == "set_status":
                new_st = msg.get("status", "online")
                if new_st in ("online", "idle", "dnd", "invisible"):
                    u_obj = db.query(User).filter(User.id == user_id).first()
                    if u_obj:
                        u_obj.status = new_st
                        db.commit()
                    visible = "offline" if new_st == "invisible" else new_st
                    for gid in guild_ids:
                        await manager.broadcast_to_guild(
                            gid,
                            {"type": "user_status", "data": {
                                "user_id": user_id, "username": username, "status": visible,
                            }},
                            exclude=user_id,
                        )
            elif msg_type == "audio_data":
                # Relay raw PCM (base64) only to other participants in the same voice channel
                ch_id   = msg.get("channel_id")
                pcm_b64 = msg.get("pcm", "")
                if isinstance(ch_id, int) and ch_id in _voice_state:
                    participants = _voice_state[ch_id]
                    if user_id in participants:
                        relay = {
                            "type": "audio_data",
                            "data": {"channel_id": ch_id, "user_id": user_id, "pcm": pcm_b64},
                        }
                        for uid in list(participants.keys()):
                            if uid != user_id:
                                await manager.send_to_user(uid, relay)
            elif msg_type == "voice_speaking":
                # Broadcast speaking state to whole guild so all viewers see the ring
                ch_id    = msg.get("channel_id")
                speaking = bool(msg.get("speaking", False))
                if isinstance(ch_id, int):
                    ch_obj = db.get(Channel, ch_id)
                    if ch_obj:
                        await manager.broadcast_to_guild(
                            ch_obj.guild_id,
                            {"type": "voice_speaking", "data": {
                                "channel_id": ch_id,
                                "user_id":    user_id,
                                "username":   username,
                                "speaking":   speaking,
                            }},
                            exclude=user_id,
                        )

    except WebSocketDisconnect:
        pass
    except asyncio.TimeoutError:
        await websocket.close(code=4002, reason="Authentication timeout")
    except Exception:
        pass
    finally:
        if user_id:
            # Remove from any voice channels and notify guild members
            voice_leave_tasks = []
            for ch_id, participants in list(_voice_state.items()):
                if user_id in participants:
                    participants.pop(user_id, None)
                    ch = db.get(Channel, ch_id)
                    if ch:
                        voice_leave_tasks.append(
                            manager.broadcast_to_guild(ch.guild_id, {
                                "type": "voice_leave",
                                "data": {"channel_id": ch_id, "user_id": user_id, "username": username},
                            })
                        )
            if voice_leave_tasks:
                await asyncio.gather(*voice_leave_tasks, return_exceptions=True)

            manager.disconnect(user_id)
            memberships = db.query(GuildMember).filter(GuildMember.user_id == user_id).all()
            offline_tasks = [
                manager.broadcast_to_guild(
                    m.guild_id,
                    {"type": "user_offline", "data": {"user_id": user_id, "username": username}},
                )
                for m in memberships
            ]
            if offline_tasks:
                await asyncio.gather(*offline_tasks, return_exceptions=True)


# ── Internal helpers ───────────────────────────────────────────────────────────

def _require_member(db: Session, guild_id: int, user_id: int) -> GuildMember:
    m = db.query(GuildMember).filter(
        GuildMember.guild_id == guild_id,
        GuildMember.user_id  == user_id,
    ).first()
    if not m:
        raise HTTPException(status_code=403, detail="Not a member of this guild")
    return m


def _get_channel_or_404(db: Session, channel_id: int) -> Channel:
    ch = db.query(Channel).filter(Channel.id == channel_id).first()
    if not ch:
        raise HTTPException(status_code=404, detail="Channel not found")
    return ch


def _msg_out(m: Message) -> MessageOut:
    author = m.author
    reply_to_author  = None
    reply_to_content = None
    if m.reply_to_id and m.reply_to:
        rt = m.reply_to
        reply_to_author  = rt.webhook_name or (rt.author.username if rt.author else None)
        reply_to_content = rt.content[:200] if rt.content else None
    return MessageOut(
        id=m.id,
        channel_id=m.channel_id,
        author_id=m.author_id,
        author_username=m.webhook_name or author.username,
        author_has_avatar=author.avatar is not None if not m.webhook_name else False,
        author_is_bot=author.is_bot,
        content=m.content,
        reply_to_id=m.reply_to_id,
        reply_to_author=reply_to_author,
        reply_to_content=reply_to_content,
        created_at=m.created_at,
        edited_at=m.edited_at,
    )


def _user_out(u: User) -> UserOut:
    return UserOut(
        id=u.id,
        username=u.username,
        email=u.email,
        display_name=u.display_name,
        bio=u.bio,
        has_avatar=u.avatar is not None,
        is_bot=u.is_bot,
        created_at=u.created_at,
        status=u.status or "online",
        kyc_status=u.kyc_status or "unverified",
    )


def _profile_out(u: User) -> UserProfileOut:
    return UserProfileOut(
        id=u.id,
        username=u.username,
        display_name=u.display_name,
        bio=u.bio,
        is_bot=u.is_bot,
        has_avatar=u.avatar is not None,
    )


def _dm_conv_out(conv: DirectConversation, my_id: int) -> DmConversationOut:
    other = conv.user2 if conv.user1_id == my_id else conv.user1
    return DmConversationOut(
        id=conv.id,
        other_user_id=other.id,
        other_username=other.username,
        other_display_name=other.display_name,
        other_has_avatar=other.avatar is not None,
    )


def _dm_msg_out(m: DirectMessage) -> DmMessageOut:
    reply_to_author  = None
    reply_to_content = None
    if m.reply_to_id and m.reply_to:
        rt = m.reply_to
        reply_to_author  = rt.author.username if rt.author else None
        reply_to_content = rt.content[:200] if rt.content else None
    return DmMessageOut(
        id=m.id,
        conversation_id=m.conversation_id,
        author_id=m.author_id,
        author_username=m.author.username,
        author_has_avatar=m.author.avatar is not None,
        author_is_bot=m.author.is_bot,
        content=m.content,
        reply_to_id=m.reply_to_id,
        reply_to_author=reply_to_author,
        reply_to_content=reply_to_content,
        created_at=m.created_at,
    )


def _bot_out(b: Bot) -> BotOut:
    return BotOut(
        id=b.id,
        user_id=b.user_id,
        username=b.bot_user.username,
        name=b.bot_user.display_name or "",
    )


def _cmd_out(c: SlashCommand) -> CommandOut:
    return CommandOut(
        id=c.id,
        guild_id=c.guild_id,
        name=c.name,
        description=c.description,
        bot_username=c.bot.bot_user.username,
    )


def _hook_out(h: Webhook) -> WebhookOut:
    return WebhookOut(
        id=h.id,
        channel_id=h.channel_id,
        name=h.name,
        token=h.token,
        url=f"/webhooks/{h.id}/{h.token}",
    )


def _guild_out(g: Guild) -> GuildOut:
    return GuildOut(
        id=g.id,
        name=g.name,
        owner_id=g.owner_id,
        invite_code=g.invite_code,
        has_icon=g.icon is not None,
        created_at=g.created_at,
    )


def _role_out(r: Role) -> RoleOut:
    return RoleOut(
        id=r.id,
        guild_id=r.guild_id,
        name=r.name,
        color=r.color,
        permissions=r.permissions,
        position=r.position,
        hoist=r.hoist,
    )


def _member_out(m: GuildMember, online_set: set, db: Session) -> MemberOut:
    profile = db.query(ServerProfile).filter(
        ServerProfile.guild_id == m.guild_id,
        ServerProfile.user_id  == m.user_id,
    ).first()
    user = m.user
    # Compute visible presence (invisible users appear as offline to others)
    phys_online = m.user_id in online_set
    pref = user.status if user.status in ("online", "idle", "dnd", "invisible") else "online"
    if phys_online and pref != "invisible":
        vis_status = pref
        is_online  = True
    else:
        vis_status = "offline"
        is_online  = False
    return MemberOut(
        user_id=m.user_id,
        username=user.username,
        display_name=user.display_name,
        nickname=profile.nickname if profile else None,
        role=m.role,
        is_online=is_online,
        has_avatar=user.avatar is not None,
        is_bot=user.is_bot,
        roles=[_role_out(mr.role) for mr in m.member_roles],
        status=vis_status,
    )


def _ban_out(b: Ban) -> BanOut:
    return BanOut(
        guild_id=b.guild_id,
        user_id=b.user_id,
        username=b.user.username,
        reason=b.reason,
    )


def _server_profile_out(sp: ServerProfile) -> ServerProfileOut:
    return ServerProfileOut(
        guild_id=sp.guild_id,
        user_id=sp.user_id,
        nickname=sp.nickname,
        has_avatar=sp.avatar is not None,
    )


def _has_perm(member: GuildMember, bit: int) -> bool:
    """Return True if member has the given permission."""
    if member.role in ("owner", "admin"):
        return True
    for mr in member.member_roles:
        if mr.role.permissions & Perms.ADMINISTRATOR:
            return True
        if mr.role.permissions & bit:
            return True
    return False


def _require_perm(member: GuildMember, bit: int):
    if not _has_perm(member, bit):
        raise HTTPException(status_code=403, detail="Insufficient permissions")


def _require_dm_access(db: Session, dm_id: int, user_id: int) -> DirectConversation:
    conv = db.query(DirectConversation).filter(DirectConversation.id == dm_id).first()
    if not conv or (conv.user1_id != user_id and conv.user2_id != user_id):
        raise HTTPException(status_code=403, detail="Not your conversation")
    return conv


def _friend_out(u: User) -> FriendOut:
    """Compute the visible status for a friend (invisible → offline to others)."""
    st = u.status or "online"
    if not manager.is_online(u.id) or st == "invisible":
        vis = "offline"
    else:
        vis = st
    return FriendOut(
        user_id=u.id,
        username=u.username,
        display_name=u.display_name,
        has_avatar=u.avatar is not None,
        status=vis,
    )


def _friend_req_out(f: Friendship) -> FriendRequestOut:
    u = f.requester
    return FriendRequestOut(
        from_user_id=u.id,
        from_username=u.username,
        from_display_name=u.display_name,
        from_has_avatar=u.avatar is not None,
    )
