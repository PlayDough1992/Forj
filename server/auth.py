from datetime import datetime, timedelta, timezone
from typing import Optional

import bcrypt
from jose import JWTError, jwt
from fastapi import Depends, HTTPException, status
from fastapi.security import OAuth2PasswordBearer
from sqlalchemy.orm import Session

from models import User
from database import get_db

# ── Change SECRET_KEY before deploying to production ──────────────────────────
SECRET_KEY = "CHANGE-THIS-IN-PRODUCTION-REPLACE-WITH-A-LONG-RANDOM-SECRET-KEY"
ALGORITHM  = "HS256"
TOKEN_EXPIRE_MINUTES = 60 * 24 * 7  # 7 days

oauth2_scheme = OAuth2PasswordBearer(tokenUrl="/auth/login")


def verify_password(plain: str, hashed: str) -> bool:
    return bcrypt.checkpw(plain.encode(), hashed.encode())


def hash_password(password: str) -> str:
    return bcrypt.hashpw(password.encode(), bcrypt.gensalt()).decode()


def create_access_token(user_id: int, token_version: int = 0, expires_delta: Optional[timedelta] = None) -> str:
    expire  = datetime.now(timezone.utc) + (expires_delta or timedelta(minutes=TOKEN_EXPIRE_MINUTES))
    payload = {"sub": str(user_id), "exp": expire, "ver": token_version}
    return jwt.encode(payload, SECRET_KEY, algorithm=ALGORITHM)


def create_bot_token(user_id: int) -> str:
    """Create a non-expiring JWT for a bot user account."""
    payload = {
        "sub": str(user_id),
        "bot": True,
        "exp": datetime(2099, 12, 31, tzinfo=timezone.utc),
    }
    return jwt.encode(payload, SECRET_KEY, algorithm=ALGORITHM)


def verify_token(token: str) -> Optional[int]:
    """Return user_id from a valid token, or None."""
    try:
        payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        raw = payload.get("sub")
        return int(raw) if raw is not None else None
    except (JWTError, ValueError, TypeError):
        return None


async def get_current_user(
    token: str = Depends(oauth2_scheme),
    db: Session = Depends(get_db),
) -> User:
    creds_exc = HTTPException(
        status_code=status.HTTP_401_UNAUTHORIZED,
        detail="Invalid or expired token",
        headers={"WWW-Authenticate": "Bearer"},
    )
    try:
        payload  = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        raw      = payload.get("sub")
        user_id  = int(raw) if raw is not None else None
        token_ver = int(payload.get("ver", 0))
    except (JWTError, ValueError, TypeError):
        raise creds_exc

    if user_id is None:
        raise creds_exc

    user = db.query(User).filter(User.id == user_id, User.is_active == True).first()
    if user is None:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="User not found")

    # Verify token version — invalidated when the user changes their password
    if token_ver != (user.token_version or 0):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Session expired. Please log in again.",
            headers={"WWW-Authenticate": "Bearer"},
        )

    return user
