from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from dependencies import get_current_user, get_db
from models.channel import Channel
from models.server import Server, ServerMember
from models.user import User
from schemas.channel import (
    ChannelAccessResponse,
    ChannelCreate,
    ChannelResponse,
    ChannelUpdate,
)

router = APIRouter(prefix="/servers/{server_id}/channels", tags=["channels"])

# Channels are also addressable without their server: the signaling server only
# knows the channel id it received in the WebSocket path.
access_router = APIRouter(prefix="/channels", tags=["channels"])


async def _require_server_member(db: AsyncSession, server_id: int, user_id: int) -> Server:
    result = await db.execute(select(Server).where(Server.id == server_id))
    server = result.scalar_one_or_none()
    if not server:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Server not found")

    member_result = await db.execute(
        select(ServerMember).where(
            ServerMember.server_id == server_id, ServerMember.user_id == user_id
        )
    )
    if not member_result.scalar_one_or_none():
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Not a server member")
    return server


async def _get_channel_in_server(db: AsyncSession, server_id: int, channel_id: int) -> Channel:
    result = await db.execute(
        select(Channel).where(Channel.id == channel_id, Channel.server_id == server_id)
    )
    channel = result.scalar_one_or_none()
    if not channel:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Channel not found")
    return channel


@access_router.get("/{channel_id}/access", response_model=ChannelAccessResponse)
async def check_channel_access(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    """Authorize a media session for one channel.

    The signaling server terminates media but has no user database, so it asks
    here whether the bearer of a token may join a channel and under which
    name. A 401/403/404 is the SFU's signal to refuse the WebSocket.
    """
    result = await db.execute(select(Channel).where(Channel.id == channel_id))
    channel = result.scalar_one_or_none()
    if not channel:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Channel not found")

    await _require_server_member(db, channel.server_id, current_user.id)
    return ChannelAccessResponse(
        channel_id=channel.id,
        server_id=channel.server_id,
        user_id=current_user.id,
        username=current_user.username,
        display_name=current_user.display_name,
    )


@router.post("/", response_model=ChannelResponse, status_code=status.HTTP_201_CREATED)
async def create_channel(
    server_id: int,
    body: ChannelCreate,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _require_server_member(db, server_id, current_user.id)
    if server.owner_id != current_user.id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can create channels"
        )

    channel = Channel(
        server_id=server_id,
        name=body.name,
        kind=body.kind,
        position=body.position,
    )
    db.add(channel)
    await db.commit()
    await db.refresh(channel)
    return channel


@router.get("/", response_model=list[ChannelResponse])
async def list_channels(
    server_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_server_member(db, server_id, current_user.id)
    result = await db.execute(
        select(Channel)
        .where(Channel.server_id == server_id)
        .order_by(Channel.position, Channel.id)
    )
    return result.scalars().all()


@router.get("/{channel_id}", response_model=ChannelResponse)
async def get_channel(
    server_id: int,
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_server_member(db, server_id, current_user.id)
    return await _get_channel_in_server(db, server_id, channel_id)


@router.patch("/{channel_id}", response_model=ChannelResponse)
async def update_channel(
    server_id: int,
    channel_id: int,
    body: ChannelUpdate,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _require_server_member(db, server_id, current_user.id)
    if server.owner_id != current_user.id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can update channels"
        )

    channel = await _get_channel_in_server(db, server_id, channel_id)
    if body.name is not None:
        channel.name = body.name
    if body.position is not None:
        channel.position = body.position
    await db.commit()
    await db.refresh(channel)
    return channel


@router.delete("/{channel_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_channel(
    server_id: int,
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _require_server_member(db, server_id, current_user.id)
    if server.owner_id != current_user.id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can delete channels"
        )

    channel = await _get_channel_in_server(db, server_id, channel_id)
    await db.delete(channel)
    await db.commit()
