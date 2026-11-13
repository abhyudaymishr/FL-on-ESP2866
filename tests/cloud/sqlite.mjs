import {DatabaseSync} from 'node:sqlite';
import {readFileSync} from 'node:fs';
// Real SQLite SQL execution with the small D1 binding surface used by the app.
// This verifies queries/transactions; it is not a substitute for workerd tests.
export function database(){
  const db=new DatabaseSync(':memory:');db.exec(readFileSync(new URL('../../cloud/migrations/0001_initial.sql',import.meta.url),'utf8'));
  function prepare(sql){return {bind(...params){return {sql,params,
    async first(){return db.prepare(sql).get(...params)||null;},
    async all(){return {results:db.prepare(sql).all(...params)};}};}};}
  return {prepare,async batch(statements){
    db.exec('BEGIN');try{const results=statements.map(s=>({meta:{changes:Number(db.prepare(s.sql).run(...s.params).changes)}}));db.exec('COMMIT');return results;}catch(e){db.exec('ROLLBACK');throw e;}
  },raw:db};
}
