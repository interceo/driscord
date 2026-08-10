from pathlib import Path


def contained_path(root: Path, *parts: str) -> Path | None:
    """Join `parts` below `root`, or return None if the result escapes it.

    Traversal is rejected, not sanitised: a request that tries to leave the
    root is a request for a file this service does not serve. Resolution runs
    after the join so symlinks pointing outside the root are rejected too.
    """
    if any(not part or part in (".", "..") or "\x00" in part for part in parts):
        return None

    resolved_root = root.resolve()
    candidate = resolved_root.joinpath(*parts).resolve()
    try:
        candidate.relative_to(resolved_root)
    except ValueError:
        return None
    return candidate
