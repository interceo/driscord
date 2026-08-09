from typing import Annotated

from pydantic import BeforeValidator

from avatar_storage import available_avatar_url


AvatarUrl = Annotated[str | None, BeforeValidator(available_avatar_url)]
