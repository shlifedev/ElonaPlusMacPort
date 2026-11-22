"""Enumerate native sources in either a working tree or a private source snapshot."""
import os
from pathlib import Path


def native_sources(source):
    excluded = {Path("src/launcher"), Path("src/localization/ko-KR")}
    for top in ("src", "cmake"):  # Older private snapshots have top-level CMake modules.
        for directory, subdirs, files in os.walk(source / top):
            rel = Path(directory).relative_to(source)
            subdirs[:] = sorted(d for d in subdirs if d != "__pycache__" and rel / d not in excluded)
            for name in sorted(files):
                path = source / rel / name
                if path.is_file() and not path.is_symlink():
                    yield path
