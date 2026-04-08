from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from dependencies import get_current_user, get_db
from models.channel import Channel, ChannelMember
from models.user import User
from schemas.channel import ChannelCreate, ChannelMemberResponse, ChannelResponse, ChannelUpdate

router = APIRouter(prefix="/channels", tags=["channels"])


@router.post("/", response_model=ChannelResponse, status_code=status.HTTP_201_CREATED)
async def create_channel(
    body: ChannelCreate,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    existing = await db.execute(select(Channel).where(Channel.name == body.name))
    if existing.scalar_one_or_none():
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Channel name already taken")

    channel = Channel(name=body.name, description=body.description, owner_id=current_user.id)
    db.add(channel)
    await db.flush()

    member = ChannelMember(channel_id=channel.id, user_id=current_user.id)
    db.add(member)
    await db.commit()
    await db.refresh(channel)
    return channel


@router.get("/", response_model=list[ChannelResponse])
async def list_channels(
    _current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(Channel).order_by(Channel.name))
    return result.scalars().all()


@router.get("/{channel_id}", response_model=ChannelResponse)
async def get_channel(
    channel_id: int,
    _current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(Channel).where(Channel.id == channel_id))
    channel = result.scalar_one_or_none()
    if not channel:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Channel not found")
    return channel


@router.patch("/{channel_id}", response_model=ChannelResponse)
async def update_channel(
    channel_id: int,
    body: ChannelUpdate,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(Channel).where(Channel.id == channel_id))
    channel = result.scalar_one_or_none()
    if not channel:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Channel not found")
    if channel.owner_id != current_user.id:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can update this channel")

    if body.name is not None:
        channel.name = body.name
    if body.description is not None:
        channel.description = body.description
    await db.commit()
    await db.refresh(channel)
    return channel


@router.delete("/{channel_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_channel(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(Channel).where(Channel.id == channel_id))
    channel = result.scalar_one_or_none()
    if not channel:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Channel not found")
    if channel.owner_id != current_user.id:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can delete this channel")

    await db.delete(channel)
    await db.commit()


@router.get("/{channel_id}/members", response_model=list[ChannelMemberResponse])
async def list_members(
    channel_id: int,
    _current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(ChannelMember, User)
        .join(User, ChannelMember.user_id == User.id)
        .where(ChannelMember.channel_id == channel_id)
    )
    return [
        ChannelMemberResponse(user_id=member.user_id, username=user.username, joined_at=member.joined_at)
        for member, user in result.all()
    ]


@router.post("/{channel_id}/members", status_code=status.HTTP_201_CREATED)
async def join_channel(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(select(Channel).where(Channel.id == channel_id))
    if not result.scalar_one_or_none():
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Channel not found")

    existing = await db.execute(
        select(ChannelMember).where(
            ChannelMember.channel_id == channel_id, ChannelMember.user_id == current_user.id
        )
    )
    if existing.scalar_one_or_none():
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Already a member")

    member = ChannelMember(channel_id=channel_id, user_id=current_user.id)
    db.add(member)
    await db.commit()
    return {"status": "joined"}


@router.delete("/{channel_id}/members", status_code=status.HTTP_204_NO_CONTENT)
async def leave_channel(
    channel_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(ChannelMember).where(
            ChannelMember.channel_id == channel_id, ChannelMember.user_id == current_user.id
        )
    )
    member = result.scalar_one_or_none()
    if not member:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Not a member")

    await db.delete(member)
    await db.commit()
