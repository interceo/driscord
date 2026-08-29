from typing import Annotated

from pydantic import AfterValidator, BeforeValidator, StringConstraints

from avatar_storage import available_avatar_url


AvatarUrl = Annotated[str | None, BeforeValidator(available_avatar_url)]


def _bcrypt_sized(value: str) -> str:
    if len(value.encode("utf-8")) > 72:
        raise ValueError("password must be at most 72 UTF-8 bytes")
    return value


Username = Annotated[
    str, StringConstraints(strip_whitespace=True, min_length=1, max_length=32)
]
Password = Annotated[str, StringConstraints(min_length=1), AfterValidator(_bcrypt_sized)]
NewPassword = Annotated[
    str, StringConstraints(min_length=6), AfterValidator(_bcrypt_sized)
]
Token = Annotated[str, StringConstraints(min_length=1, max_length=4096)]
DisplayName = Annotated[
    str, StringConstraints(strip_whitespace=True, min_length=1, max_length=64)
]
ServerName = Annotated[
    str, StringConstraints(strip_whitespace=True, min_length=1, max_length=64)
]
ServerDescription = Annotated[
    str, StringConstraints(strip_whitespace=True, max_length=256)
]
ChannelName = Annotated[
    str, StringConstraints(strip_whitespace=True, min_length=1, max_length=64)
]
