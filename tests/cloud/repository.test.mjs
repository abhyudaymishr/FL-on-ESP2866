import test from 'node:test';import assert from 'node:assert/strict';
import {snapshot,NOW} from './fixtures.mjs';import {database} from './sqlite.mjs';
import {TelemetryRepository} from '../../cloud/repository.mjs';
test('actual SQL accepts a snapshot, rejects old arrivals and never refreshes a replay',async()=>{
  const db=database();try{const repo=new TelemetryRepository(db);
    assert.equal(await repo.save(snapshot(),NOW),true);
    assert.equal(await repo.save(snapshot(),NOW+20000),false);
    assert.equal(await repo.save(snapshot(NOW-1000),NOW+20000),false);
    assert.equal((await repo.latest('lab-01')).receivedAt,NOW);
    assert.equal((await repo.history('lab-01',0)).length,1);
  }finally{db.raw.close();}
});
test('fixed history ring overwrites expired slots and downsamples within a minute',async()=>{
  const db=database();try{const repo=new TelemetryRepository(db);
    await repo.save(snapshot(),NOW);await repo.save(snapshot(NOW+1000),NOW+1000);
    assert.equal((await repo.history('lab-01',0)).length,1);
    assert.equal((await repo.history('lab-01',0))[0].capturedAt,NOW);
    for(let i=1;i<=1441;i++)await repo.save(snapshot(NOW+i*60000),NOW+i*60000);
    assert.equal((await repo.history('lab-01',0)).length,1440);
    assert.equal((await repo.latest('lab-01')).snapshot.capturedAt,NOW+1441*60000);
    assert.equal((await repo.history('lab-01',NOW+1440*60000)).length,2);
  }finally{db.raw.close();}
});
test('D1 batch shape executes atomically on failure',async()=>{
  const db=database();try{
    await assert.rejects(db.batch([db.prepare('INSERT INTO gateway_latest VALUES (?, ?, ?, ?)').bind('lab-01',1,1,'{}'),db.prepare('INVALID SQL').bind()]));
    assert.equal((await new TelemetryRepository(db).latest('lab-01')).snapshot,null);
  }finally{db.raw.close();}
});
