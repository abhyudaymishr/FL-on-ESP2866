import {historyPoint, LIMITS} from '../shared/domain.mjs';
export class TelemetryRepository {
  constructor(db) { this.db=db; }
  async verifySchema(expected) {
    const {results}=await this.db.prepare('SELECT name FROM schema_migrations').bind().all();
    const applied=new Set(results.map(row=>row.name));
    return expected.every(name=>applied.has(name));
  }
  async save(snapshot, now) {
    const point=historyPoint(snapshot), slot=Math.floor(snapshot.capturedAt/60000)%LIMITS.historySlots;
    // D1 batch is transactional. History is written only if this exact snapshot
    // won the latest-state compare-and-swap; retries do not refresh liveness.
    const results=await this.db.batch([
      this.db.prepare(`INSERT INTO gateway_latest VALUES (?, ?, ?, ?)
        ON CONFLICT(gateway_id) DO UPDATE SET captured_at=excluded.captured_at,
        received_at=excluded.received_at, snapshot_json=excluded.snapshot_json
        WHERE excluded.captured_at > gateway_latest.captured_at`).bind(snapshot.gatewayId,snapshot.capturedAt,now,JSON.stringify(snapshot)),
      this.db.prepare(`INSERT INTO gateway_history
        SELECT ?, ?, ?, ? WHERE EXISTS (SELECT 1 FROM gateway_latest WHERE gateway_id=? AND captured_at=?)
        ON CONFLICT(gateway_id,slot) DO UPDATE SET captured_at=excluded.captured_at, point_json=excluded.point_json
        WHERE CAST(excluded.captured_at/60000 AS INTEGER) > CAST(gateway_history.captured_at/60000 AS INTEGER)`)
        .bind(snapshot.gatewayId,slot,snapshot.capturedAt,JSON.stringify(point),snapshot.gatewayId,snapshot.capturedAt)
    ]);
    return results[0].meta.changes>0;
  }
  async latest(gatewayId) {
    const row=await this.db.prepare('SELECT received_at, snapshot_json FROM gateway_latest WHERE gateway_id=?').bind(gatewayId).first();
    return row ? {snapshot:JSON.parse(row.snapshot_json),receivedAt:row.received_at} : {snapshot:null,receivedAt:null};
  }
  async history(gatewayId,since) {
    // Scan at most 1,440 compact rows by the leftmost primary-key prefix.
    const data=await this.db.prepare('SELECT point_json FROM gateway_history WHERE gateway_id=? AND captured_at>=? ORDER BY captured_at').bind(gatewayId,since).all();
    return data.results.map(row=>JSON.parse(row.point_json));
  }
}
