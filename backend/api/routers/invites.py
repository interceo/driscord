import secrets
import string

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from dependencies import get_current_user, get_db
from models.server import Server, ServerInvite, ServerMember
from models.user import User
from schemas.server import InviteAcceptResponse, ServerInviteResponse

_ALPHABET = string.ascii_letters + string.digits
_CODE_LEN = 8
_MAX_GEN_ATTEMPTS = 5


def _generate_code() -> str:
    return "".join(secrets.choice(_ALPHABET) for _ in range(_CODE_LEN))


async def _get_server_or_404(db: AsyncSession, server_id: int) -> Server:
    result = await db.execute(select(Server).where(Server.id == server_id))
    server = result.scalar_one_or_none()
    if not server:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Server not found")
    return server


async def _require_server_member(db: AsyncSession, server_id: int, user_id: int) -> Server:
    server = await _get_server_or_404(db, server_id)
    member_result = await db.execute(
        select(ServerMember).where(
            ServerMember.server_id == server_id, ServerMember.user_id == user_id
        )
    )
    if not member_result.scalar_one_or_none():
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a server member")
    return server


server_invites_router = APIRouter(prefix="/servers/{server_id}/invites", tags=["invites"])
invites_router = APIRouter(prefix="/invites", tags=["invites"])


@server_invites_router.post(
    "/", response_model=ServerInviteResponse, status_code=status.HTTP_201_CREATED
)
async def create_invite(
    server_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_server_member(db, server_id, current_user.id)

    for _ in range(_MAX_GEN_ATTEMPTS):
        invite = ServerInvite(
            code=_generate_code(), server_id=server_id, creator_id=current_user.id
        )
        db.add(invite)
        try:
            await db.commit()
        except IntegrityError:
            await db.rollback()
            continue
        await db.refresh(invite)
        return invite

    raise HTTPException(
        status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
        detail="Failed to generate a unique invite code",
    )


@server_invites_router.get("/", response_model=list[ServerInviteResponse])
async def list_invites(
    server_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _get_server_or_404(db, server_id)
    if server.owner_id != current_user.id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can list invites"
        )
    result = await db.execute(
        select(ServerInvite)
        .where(ServerInvite.server_id == server_id)
        .order_by(ServerInvite.created_at.desc())
    )
    return result.scalars().all()


@server_invites_router.delete("/{code}", status_code=status.HTTP_204_NO_CONTENT)
async def revoke_invite(
    server_id: int,
    code: str,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _get_server_or_404(db, server_id)
    result = await db.execute(
        select(ServerInvite).where(
            ServerInvite.server_id == server_id, ServerInvite.code == code
        )
    )
    invite = result.scalar_one_or_none()
    if not invite:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Invite not found")

    if server.owner_id != current_user.id and invite.creator_id != current_user.id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Only the owner or the invite creator can revoke it",
        )

    await db.delete(invite)
    await db.commit()


@invites_router.post("/{code}", response_model=InviteAcceptResponse)
async def accept_invite(
    code: str,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(ServerInvite).where(ServerInvite.code == code))
    invite = result.scalar_one_or_none()
    if not invite:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Invalid invite code")

    existing = await db.execute(
        select(ServerMember).where(
            ServerMember.server_id == invite.server_id,
            ServerMember.user_id == current_user.id,
        )
    )
    if existing.scalar_one_or_none():
        return InviteAcceptResponse(server_id=invite.server_id, status="already_member")

    db.add(ServerMember(server_id=invite.server_id, user_id=current_user.id))
    await db.commit()
    return InviteAcceptResponse(server_id=invite.server_id, status="joined")
