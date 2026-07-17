"""Pure comfort-algorithm package (Phase 3).

Every module here is synchronous with no side effects — no Redis/Postgres/MQTT
I/O. Callers (Phase 4's MQTT worker, Phase 5's preview endpoint) gather state
first, then call ``comfort_engine.compute()``. See
plans/260714-2007-adaptive-biothermal-control-2node/phase-03-comfort-algorithm.md
for the full design.
"""
