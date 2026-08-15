"""App Lab's entry point. Everything real lives in the ``edge_ai`` package next to this
file.

That package must NOT be edited here — it is a copy poured in by
``edge-ai/deploy/build-applab-app.py``, the same source as the systemd deployment.
Editing it here means the next build wipes the change, and worse, the two deployments
quietly diverge.

CONFIGURATION is read from ``python/.env`` (see ``_load_dotenv`` in
``edge_ai/main.py``). That file is not in git and is not created by the build script —
it holds the household's ORG_ID. Without it the service stops immediately with a line
naming the missing variable, rather than carrying on with a guessed value: a wrong
ORG_ID means every command sent down is silently refused by the gateway because the
``link_key`` does not match.
"""

import sys

# App Lab reads the log through the container's stdout, and a container is not a tty so
# Python buffers by BLOCK. Without these lines the log appears in 4KB chunks — which,
# while debugging, looks exactly like the service having hung.
sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)

from edge_ai.main import main  # noqa: E402  (must come after the buffering setup above)

sys.exit(main())
