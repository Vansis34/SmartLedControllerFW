"""PlatformIO pre-build hook for the embedded web interface."""

from pathlib import Path
import runpy

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


project_directory = Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]
generator_path = project_directory / "main" / "www" / "zipzip.py"
generator = runpy.run_path(str(generator_path))
changed = generator["update_web_asset"]()

status = "updated" if changed else "up to date"
print(f"Embedded web asset: {status}")
