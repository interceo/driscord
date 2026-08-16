from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from dependencies import get_current_user, get_db
from models.server import Server, ServerMember
from models.user import User
from schemas.server import ServerCreate, ServerMemberResponse, ServerResponse, ServerUpdate

router = APIRouter(prefix="/servers", tags=["servers"])


async def _get_server_or_404(db: AsyncSession, server_id: int) -> Server:
    result = await db.execute(select(Server).where(Server.id == server_id))
    server = result.scalar_one_or_none()
    if not server:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Server not found")
    return server


async def _require_server_member(
    db: AsyncSession, server_id: int, user_id: int
) -> Server:
    server = await _get_server_or_404(db, server_id)
    membership = await db.execute(
        select(ServerMember).where(
            ServerMember.server_id == server_id,
            ServerMember.user_id == user_id,
        )
    )
    if not membership.scalar_one_or_none():
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN, detail="Not a server member"
        )
    return server


@router.post("/", response_model=ServerResponse, status_code=status.HTTP_201_CREATED)
async def create_server(
    body: ServerCreate,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = Server(name=body.name, description=body.description, owner_id=current_user.id)
    db.add(server)
    await db.flush()

    member = ServerMember(server_id=server.id, user_id=current_user.id)
    db.add(member)
    await db.commit()
    await db.refresh(server)
    return server


@router.get("/", response_model=list[ServerResponse])
async def list_my_servers(
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    result = await db.execute(
        select(Server)
        .join(ServerMember, ServerMember.server_id == Server.id)
        .where(ServerMember.user_id == current_user.id)
        .order_by(Server.name)
    )
    return result.scalars().all()


@router.get("/{server_id}", response_model=ServerResponse)
async def get_server(
    server_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    return await _require_server_member(db, server_id, current_user.id)


@router.patch("/{server_id}", response_model=ServerResponse)
async def update_server(
    server_id: int,
    body: ServerUpdate,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _get_server_or_404(db, server_id)
    if server.owner_id != current_user.id:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can update this server")

    if body.name is not None:
        server.name = body.name
    if "description" in body.model_fields_set:
        server.description = body.description
    await db.commit()
    await db.refresh(server)
    return server


@router.delete("/{server_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_server(
    server_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _get_server_or_404(db, server_id)
    if server.owner_id != current_user.id:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Only the owner can delete this server")

    await db.delete(server)
    await db.commit()


@router.get("/{server_id}/members", response_model=list[ServerMemberResponse])
async def list_members(
    server_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    await _require_server_member(db, server_id, current_user.id)
    result = await db.execute(
        select(ServerMember, User)
        .join(User, ServerMember.user_id == User.id)
        .where(ServerMember.server_id == server_id)
    )
    return [
        ServerMemberResponse(user_id=member.user_id, username=user.username, joined_at=member.joined_at)
        for member, user in result.all()
    ]


@router.post("/{server_id}/members", status_code=status.HTTP_201_CREATED)
async def join_server(
    server_id: int,
    _current_user: User = Depends(get_current_user),
):
    # Keep an explicit response for old clients instead of silently turning the
    # route into 405. Membership is granted only by accepting an invite or by an
    # owner adding a known user; knowing an incremental server id is not access.
    raise HTTPException(
        status_code=status.HTTP_403_FORBIDDEN,
        detail="An invite is required to join this server",
    )


@router.post("/{server_id}/members/{user_id}", status_code=status.HTTP_201_CREATED)
async def add_server_member(
    server_id: int,
    user_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _get_server_or_404(db, server_id)
    if server.owner_id != current_user.id:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Only the owner can add members",
        )

    user_result = await db.execute(select(User).where(User.id == user_id))
    if not user_result.scalar_one_or_none():
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="User not found")

    existing = await db.execute(
        select(ServerMember).where(
            ServerMember.server_id == server_id,
            ServerMember.user_id == user_id,
        )
    )
    if existing.scalar_one_or_none():
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="Already a member")

    db.add(ServerMember(server_id=server_id, user_id=user_id))
    await db.commit()
    return {"status": "added", "user_id": user_id}


@router.delete("/{server_id}/members", status_code=status.HTTP_204_NO_CONTENT)
async def leave_server(
    server_id: int,
    current_user: User = Depends(get_current_user),
    db: AsyncSession = Depends(get_db),
):
    server = await _get_server_or_404(db, server_id)
    if server.owner_id == current_user.id:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Owner cannot leave the server; delete it instead",
        )

    result = await db.execute(
        select(ServerMember).where(
            ServerMember.server_id == server_id, ServerMember.user_id == current_user.id
        )
    )
    member = result.scalar_one_or_none()
    if not member:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Not a member")

    await db.delete(member)
    await db.commit()
