from sqlalchemy import (
    Column, Integer, String, Text, DateTime, Boolean,
    ForeignKey, UniqueConstraint, LargeBinary, BigInteger,
)
from sqlalchemy.orm import relationship
from datetime import datetime, timezone
from database import Base


def _utcnow():
    return datetime.now(timezone.utc)


class User(Base):
    __tablename__ = "users"

    id              = Column(Integer, primary_key=True, index=True)
    username        = Column(String(32),  unique=True, index=True, nullable=False)
    email           = Column(String(255), unique=True, index=True, nullable=False)
    hashed_password = Column(String(255), nullable=False)
    display_name    = Column(String(32),  nullable=True)
    bio             = Column(String(500), nullable=True)
    avatar          = Column(LargeBinary, nullable=True)
    avatar_type     = Column(String(30),  nullable=True)   # e.g. "image/png"
    is_bot          = Column(Boolean, default=False, nullable=False)
    created_at      = Column(DateTime(timezone=True), default=_utcnow, nullable=False)
    is_active       = Column(Boolean, default=True,  nullable=False)
    status          = Column(String(10), default="online", nullable=False)
    # KYC / identity verification
    kyc_status      = Column(String(12), default="unverified", nullable=False)  # unverified|pending|approved|declined
    kyc_session_id  = Column(String(100), nullable=True)
    date_of_birth   = Column(String(10),  nullable=True)   # ISO date YYYY-MM-DD from ID doc
    # Token invalidation (incremented on password change so old JWTs are rejected)
    token_version   = Column(Integer, default=0, nullable=False)

    memberships = relationship("GuildMember", back_populates="user",   cascade="all, delete-orphan")
    messages    = relationship("Message",     back_populates="author", cascade="all, delete-orphan")
    owned_bots  = relationship("Bot", back_populates="owner",
                               foreign_keys="Bot.owner_id", cascade="all, delete-orphan")


class Guild(Base):
    __tablename__ = "guilds"

    id          = Column(Integer, primary_key=True, index=True)
    name        = Column(String(100), nullable=False)
    owner_id    = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    invite_code = Column(String(16), unique=True, index=True, nullable=False)
    icon        = Column(LargeBinary, nullable=True)
    icon_type   = Column(String(30),  nullable=True)
    created_at  = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    owner    = relationship("User",        foreign_keys=[owner_id])
    members  = relationship("GuildMember", back_populates="guild", cascade="all, delete-orphan")
    channels = relationship("Channel",     back_populates="guild", cascade="all, delete-orphan")
    commands = relationship("SlashCommand",back_populates="guild", cascade="all, delete-orphan")
    roles    = relationship("Role",        back_populates="guild", cascade="all, delete-orphan")
    bans     = relationship("Ban",         back_populates="guild", cascade="all, delete-orphan")

    @property
    def has_icon(self) -> bool:
        return self.icon is not None


class GuildMember(Base):
    __tablename__ = "guild_members"
    __table_args__ = (UniqueConstraint("guild_id", "user_id", name="uq_guild_member"),)

    id        = Column(Integer, primary_key=True, index=True)
    guild_id  = Column(Integer, ForeignKey("guilds.id", ondelete="CASCADE"), nullable=False)
    user_id   = Column(Integer, ForeignKey("users.id",  ondelete="CASCADE"), nullable=False)
    role      = Column(String(10), default="member", nullable=False)
    joined_at = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    guild        = relationship("Guild", back_populates="members")
    user         = relationship("User",  back_populates="memberships")
    member_roles = relationship("MemberRole", back_populates="member", cascade="all, delete-orphan")


class Channel(Base):
    __tablename__ = "channels"

    id           = Column(Integer, primary_key=True, index=True)
    guild_id     = Column(Integer, ForeignKey("guilds.id", ondelete="CASCADE"), nullable=False)
    name         = Column(String(100), nullable=False)
    channel_type = Column(String(10), default="text", nullable=False)
    created_at   = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    guild    = relationship("Guild",   back_populates="channels")
    messages = relationship("Message", back_populates="channel",  cascade="all, delete-orphan")
    webhooks = relationship("Webhook", back_populates="channel",  cascade="all, delete-orphan")


class Message(Base):
    __tablename__ = "messages"

    id           = Column(Integer, primary_key=True, index=True)
    channel_id   = Column(Integer, ForeignKey("channels.id", ondelete="CASCADE"), nullable=False)
    author_id    = Column(Integer, ForeignKey("users.id",    ondelete="CASCADE"), nullable=False)
    content      = Column(Text, nullable=False)
    created_at   = Column(DateTime(timezone=True), default=_utcnow, nullable=False)
    edited_at    = Column(DateTime(timezone=True), nullable=True)
    webhook_name = Column(String(80), nullable=True)   # set when posted by a webhook
    reply_to_id  = Column(Integer, ForeignKey("messages.id", ondelete="SET NULL"), nullable=True)

    channel  = relationship("Channel", back_populates="messages")
    author   = relationship("User",    back_populates="messages")
    reply_to = relationship("Message", foreign_keys=[reply_to_id], remote_side=[id], uselist=False)


# ── Direct Messages ───────────────────────────────────────────────────────────

class DirectConversation(Base):
    __tablename__ = "direct_conversations"
    __table_args__ = (UniqueConstraint("user1_id", "user2_id", name="uq_dm_pair"),)

    id       = Column(Integer, primary_key=True, index=True)
    # user1_id is always the smaller id (canonical ordering)
    user1_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    user2_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    created_at = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    user1    = relationship("User", foreign_keys=[user1_id])
    user2    = relationship("User", foreign_keys=[user2_id])
    messages = relationship("DirectMessage", back_populates="conversation",
                            cascade="all, delete-orphan")


class DirectMessage(Base):
    __tablename__ = "direct_messages"

    id              = Column(Integer, primary_key=True, index=True)
    conversation_id = Column(Integer, ForeignKey("direct_conversations.id", ondelete="CASCADE"),
                             nullable=False)
    author_id       = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    content         = Column(Text, nullable=False)
    created_at      = Column(DateTime(timezone=True), default=_utcnow, nullable=False)
    reply_to_id     = Column(Integer, ForeignKey("direct_messages.id", ondelete="SET NULL"), nullable=True)

    conversation = relationship("DirectConversation", back_populates="messages")
    author       = relationship("User", foreign_keys=[author_id])
    reply_to     = relationship("DirectMessage", foreign_keys=[reply_to_id], remote_side=[id], uselist=False)


# ── Bots ──────────────────────────────────────────────────────────────────────

class Bot(Base):
    """A bot is a special User (is_bot=True) owned by a human user."""
    __tablename__ = "bots"

    id         = Column(Integer, primary_key=True, index=True)
    user_id    = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"),
                        nullable=False, unique=True)
    owner_id   = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    created_at = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    bot_user = relationship("User", foreign_keys=[user_id])
    owner    = relationship("User", foreign_keys=[owner_id], back_populates="owned_bots")
    commands = relationship("SlashCommand", back_populates="bot", cascade="all, delete-orphan")


# ── Slash Commands ────────────────────────────────────────────────────────────

class SlashCommand(Base):
    __tablename__ = "slash_commands"
    __table_args__ = (UniqueConstraint("guild_id", "name", name="uq_guild_cmd"),)

    id          = Column(Integer, primary_key=True, index=True)
    guild_id    = Column(Integer, ForeignKey("guilds.id", ondelete="CASCADE"), nullable=False)
    bot_id      = Column(Integer, ForeignKey("bots.id",   ondelete="CASCADE"), nullable=False)
    name        = Column(String(32),  nullable=False)
    description = Column(String(200), nullable=False)
    created_at  = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    guild = relationship("Guild", back_populates="commands")
    bot   = relationship("Bot",   back_populates="commands")


# ── Webhooks ──────────────────────────────────────────────────────────────────

class Webhook(Base):
    __tablename__ = "webhooks"

    id         = Column(Integer, primary_key=True, index=True)
    channel_id = Column(Integer, ForeignKey("channels.id", ondelete="CASCADE"), nullable=False)
    name       = Column(String(80),  nullable=False)
    token      = Column(String(64),  unique=True, index=True, nullable=False)
    created_by = Column(Integer, ForeignKey("users.id", ondelete="SET NULL"), nullable=True)
    created_at = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    channel = relationship("Channel", back_populates="webhooks")
    creator = relationship("User",    foreign_keys=[created_by])


# ── Roles ─────────────────────────────────────────────────────────────────────

# Permissions bitmask constants
class Perms:
    SEND_MESSAGES   = 1 << 0   # 1
    MANAGE_MESSAGES = 1 << 1   # 2
    MANAGE_CHANNELS = 1 << 2   # 4
    KICK_MEMBERS    = 1 << 3   # 8
    BAN_MEMBERS     = 1 << 4   # 16
    MANAGE_ROLES    = 1 << 5   # 32
    MANAGE_GUILD    = 1 << 6   # 64
    ADMINISTRATOR   = 1 << 7   # 128
    MANAGE_WEBHOOKS = 1 << 8   # 256
    MANAGE_BOTS     = 1 << 9   # 512
    INVITE_MEMBERS  = 1 << 10  # 1024


# ── Friendships ────────────────────────────────────────────────────────────────────

class Friendship(Base):
    __tablename__ = "friendships"
    __table_args__ = (UniqueConstraint("requester_id", "addressee_id", name="uq_friendship"),)

    id           = Column(Integer, primary_key=True, index=True)
    requester_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    addressee_id = Column(Integer, ForeignKey("users.id", ondelete="CASCADE"), nullable=False)
    status       = Column(String(10), default="pending", nullable=False)  # pending / accepted
    created_at   = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    requester = relationship("User", foreign_keys=[requester_id])
    addressee = relationship("User", foreign_keys=[addressee_id])


class Role(Base):
    __tablename__ = "roles"

    id          = Column(Integer, primary_key=True, index=True)
    guild_id    = Column(Integer, ForeignKey("guilds.id", ondelete="CASCADE"), nullable=False)
    name        = Column(String(50), nullable=False)
    color       = Column(String(7), default="#99AAB5", nullable=False)
    permissions = Column(Integer, default=0, nullable=False)
    position    = Column(Integer, default=0, nullable=False)
    hoist       = Column(Boolean, default=False, nullable=False)
    created_at  = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    guild   = relationship("Guild", back_populates="roles")
    members = relationship("MemberRole", back_populates="role", cascade="all, delete-orphan")


class MemberRole(Base):
    __tablename__ = "member_roles"
    __table_args__ = (UniqueConstraint("member_id", "role_id", name="uq_member_role"),)

    id        = Column(Integer, primary_key=True, index=True)
    member_id = Column(Integer, ForeignKey("guild_members.id", ondelete="CASCADE"), nullable=False)
    role_id   = Column(Integer, ForeignKey("roles.id",         ondelete="CASCADE"), nullable=False)

    member = relationship("GuildMember", back_populates="member_roles")
    role   = relationship("Role",        back_populates="members")


# ── Per-server profiles ───────────────────────────────────────────────────────

class ServerProfile(Base):
    __tablename__ = "server_profiles"
    __table_args__ = (UniqueConstraint("guild_id", "user_id", name="uq_server_profile"),)

    id          = Column(Integer, primary_key=True, index=True)
    guild_id    = Column(Integer, ForeignKey("guilds.id", ondelete="CASCADE"), nullable=False)
    user_id     = Column(Integer, ForeignKey("users.id",  ondelete="CASCADE"), nullable=False)
    nickname    = Column(String(32), nullable=True)
    avatar      = Column(LargeBinary, nullable=True)
    avatar_type = Column(String(30),  nullable=True)

    guild = relationship("Guild")
    user  = relationship("User")


# ── Bans ──────────────────────────────────────────────────────────────────────

class Ban(Base):
    __tablename__ = "bans"
    __table_args__ = (UniqueConstraint("guild_id", "user_id", name="uq_guild_ban"),)

    id         = Column(Integer, primary_key=True, index=True)
    guild_id   = Column(Integer, ForeignKey("guilds.id", ondelete="CASCADE"), nullable=False)
    user_id    = Column(Integer, ForeignKey("users.id",  ondelete="CASCADE"), nullable=False)
    reason     = Column(String(255), nullable=True)
    banned_by  = Column(Integer, ForeignKey("users.id",  ondelete="SET NULL"), nullable=True)
    created_at = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    guild  = relationship("Guild", back_populates="bans")
    user   = relationship("User", foreign_keys=[user_id])
    banner = relationship("User", foreign_keys=[banned_by])


# ── Child Safety ──────────────────────────────────────────────────────────────

class FrozenConversation(Base):
    """A DM conversation frozen by the safety system due to grooming detection."""
    __tablename__ = "frozen_conversations"

    id              = Column(Integer, primary_key=True, index=True)
    conversation_id = Column(Integer, ForeignKey("direct_conversations.id", ondelete="CASCADE"),
                             unique=True, nullable=False)
    reason          = Column(Text, nullable=False)
    frozen_at       = Column(DateTime(timezone=True), default=_utcnow, nullable=False)

    conversation = relationship("DirectConversation")


class SafetyFlag(Base):
    """Individual grooming pattern trigger logged per message."""
    __tablename__ = "safety_flags"

    id              = Column(Integer, primary_key=True, index=True)
    conversation_id = Column(Integer, ForeignKey("direct_conversations.id", ondelete="CASCADE"),
                             nullable=False)
    message_content = Column(Text,        nullable=False)
    patterns        = Column(String(500), nullable=False)   # comma-separated pattern names
    severity_score  = Column(Integer,     nullable=False)
    created_at      = Column(DateTime(timezone=True), default=_utcnow, nullable=False)
