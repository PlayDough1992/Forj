import json
from typing import Dict, Set
from fastapi import WebSocket


class ConnectionManager:
    """Tracks live WebSocket connections and guild channel subscriptions."""

    def __init__(self) -> None:
        self._connections: Dict[int, WebSocket] = {}        # user_id  -> socket
        self._guild_members: Dict[int, Set[int]] = {}       # guild_id -> {user_ids}

    # ── Connection lifecycle ───────────────────────────────────────────────────

    def register(self, user_id: int, websocket: WebSocket) -> None:
        """Register an already-accepted WebSocket for a user."""
        self.disconnect(user_id)          # evict stale connection if any
        self._connections[user_id] = websocket

    def disconnect(self, user_id: int) -> None:
        """Remove a user's connection and all guild subscriptions."""
        self._connections.pop(user_id, None)
        for members in self._guild_members.values():
            members.discard(user_id)

    def is_online(self, user_id: int) -> bool:
        return user_id in self._connections

    # ── Guild subscriptions ────────────────────────────────────────────────────

    def subscribe(self, user_id: int, guild_id: int) -> None:
        self._guild_members.setdefault(guild_id, set()).add(user_id)

    def online_in_guild(self, guild_id: int) -> Set[int]:
        return {
            uid for uid in self._guild_members.get(guild_id, set())
            if uid in self._connections
        }

    # ── Sending ────────────────────────────────────────────────────────────────

    async def broadcast_to_guild(
        self,
        guild_id: int,
        payload: dict,
        exclude: int | None = None,
    ) -> None:
        targets = list(self._guild_members.get(guild_id, set()))
        raw = json.dumps(payload)
        for uid in targets:
            if uid == exclude or uid not in self._connections:
                continue
            try:
                await self._connections[uid].send_text(raw)
            except Exception:
                self.disconnect(uid)

    async def send_to_user(self, user_id: int, payload: dict) -> None:
        ws = self._connections.get(user_id)
        if ws is None:
            return
        try:
            await ws.send_text(json.dumps(payload))
        except Exception:
            self.disconnect(user_id)


manager = ConnectionManager()
