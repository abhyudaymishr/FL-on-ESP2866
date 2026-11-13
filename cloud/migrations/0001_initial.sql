CREATE TABLE IF NOT EXISTS schema_migrations (
  name TEXT PRIMARY KEY,
  applied_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS gateway_latest (
  gateway_id TEXT PRIMARY KEY,
  captured_at INTEGER NOT NULL,
  received_at INTEGER NOT NULL,
  snapshot_json TEXT NOT NULL CHECK(json_valid(snapshot_json))
);
-- A fixed ring, not an append-only log. No cleanup scans or delete budget.
CREATE TABLE IF NOT EXISTS gateway_history (
  gateway_id TEXT NOT NULL,
  slot INTEGER NOT NULL CHECK(slot >= 0 AND slot < 1440),
  captured_at INTEGER NOT NULL,
  point_json TEXT NOT NULL CHECK(json_valid(point_json)),
  PRIMARY KEY (gateway_id, slot)
) WITHOUT ROWID;
