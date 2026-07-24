"""Create the deterministic gzip asset embedded by the ESP-IDF firmware."""

from __future__ import annotations

import gzip
import io
import os
from pathlib import Path


WEB_DIRECTORY = Path(__file__).resolve().parent
HTML_PATH = WEB_DIRECTORY / "index.html"
GZIP_PATH = WEB_DIRECTORY / "index.html.gz"


def compress_deterministically(source: bytes) -> bytes:
    """Return gzip bytes whose header does not depend on time or file name."""

    output = io.BytesIO()
    # Empty filename and mtime=0 make identical HTML produce identical bytes
    # on every machine and every build.
    with gzip.GzipFile(
        filename="",
        mode="wb",
        compresslevel=9,
        fileobj=output,
        mtime=0,
    ) as archive:
        archive.write(source)
    return output.getvalue()


def update_web_asset(
    html_path: Path = HTML_PATH,
    gzip_path: Path = GZIP_PATH,
) -> bool:
    """Update the embedded gzip file only when its content actually changed.

    Returns:
        True when the output file was replaced, otherwise False.
    """

    compressed = compress_deterministically(html_path.read_bytes())
    if gzip_path.exists() and gzip_path.read_bytes() == compressed:
        return False

    temporary_path = gzip_path.with_suffix(gzip_path.suffix + ".tmp")
    temporary_path.write_bytes(compressed)
    # os.replace is atomic on the target filesystem: an interrupted build
    # cannot leave index.html.gz partially written.
    os.replace(temporary_path, gzip_path)
    return True


def main() -> None:
    """Generate the web asset and print a concise standalone status."""

    changed = update_web_asset()
    action = "updated" if changed else "already up to date"
    print(f"{GZIP_PATH.name}: {action}")


if __name__ == "__main__":
    main()
