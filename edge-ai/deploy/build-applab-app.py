"""Build the Arduino App Lab App directory from the source in the repo.

    python edge-ai/deploy/build-applab-app.py

The result lands in ``edge-ai/applab/BreezeLink/`` — push it to the board with:

    bash scripts/push-unoq-app.sh edge-ai/applab/BreezeLink

WHY IT IS BUILT RATHER THAN COMMITTED READY-MADE: App Lab requires the whole source
tree to sit inside the app directory (it packages the entire directory before running
it), but ``edge_ai``, the backend's ``app/`` slice and the protocol header all already
have an owner elsewhere in the repo. Copying them in by hand creates a second copy, and
a copy drifts from the original — exactly what ``comfort_bridge.py`` itself was written
to avoid.

WHAT IS SOURCE AND WHAT IS PRODUCT (see edge-ai/applab/.gitignore):

    BreezeLink/
    ├── app.yaml                      source
    ├── sketch/sketch.ino             source
    ├── sketch/sketch.yaml            source
    ├── sketch/unoq-link-protocol.h   GENERATED  <- Firmware/shared/
    ├── python/main.py                source
    ├── python/requirements.txt       source
    ├── python/.env                   the household's config, NOT in git, NEVER deleted
    ├── python/edge_ai/               GENERATED  <- edge-ai/edge_ai/
    └── python/app/                   GENERATED  <- src/app/ (the comfort slice)
"""

import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path


def _load(path: Path):
    """Load a .py file whose name contains a hyphen (so it cannot be imported directly)."""
    spec = importlib.util.spec_from_file_location(path.stem.replace("-", "_"), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Reuse the systemd build's own builder. Both deployments MUST carry the same slice: a
# second file list here would be the place for them to diverge, and the symptom would be
# "it works under systemd but not inside App Lab".
_payload = _load(Path(__file__).resolve().parent / "build-edge-payload.py")

REPO = Path(__file__).resolve().parents[2]
APP = REPO / "edge-ai" / "applab" / "BreezeLink"
SHARED_HEADER = REPO / "Firmware" / "shared" / "unoq-link-protocol.h"

# Only these directories are wiped and rebuilt. Do NOT wipe all of python/: it holds
# main.py, requirements.txt and — most importantly — .env with the household's ORG_ID,
# which has no other copy to restore from.
_GENERATED = ["python/edge_ai", "python/app"]


def build() -> int:
    if not APP.is_dir():
        print(f"MISSING {APP} - this is source, not a build product.", file=sys.stderr)
        return 1
    if not SHARED_HEADER.is_file():
        print(f"MISSING {SHARED_HEADER}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        staging = Path(tmp) / "payload"
        rc = _payload.build(staging)
        if rc != 0:
            return rc

        for rel in _GENERATED:
            dst = APP / rel
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(staging / Path(rel).name, dst)

    # The sketch needs the protocol header sitting NEXT TO it: arduino-cli only compiles
    # what is inside the sketch directory, with no include path reaching outside the
    # board repo.
    shutil.copy2(SHARED_HEADER, APP / "sketch" / SHARED_HEADER.name)

    n = sum(1 for _ in APP.rglob("*") if _.is_file())
    print(f"{APP}: {n} files")

    if not (APP / "python" / ".env").is_file():
        print(
            "\nCHUA CO python/.env — dich vu se dung ngay khi chay voi:\n"
            "    Thieu bien moi truong bat buoc: EDGE_ORG_ID\n"
            "Tao file do truoc khi day len bo (xem edge-ai/README.md).",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(build())
