// Integration check against an isolated portable Polden instance.
// Pass its runtime directory. The test profile must use split-file recording.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile, readdir } from 'node:fs/promises';
import path from 'node:path';

const runtime = path.resolve(process.argv[2]);
const config = path.join(runtime, 'config/obs-studio');
const wsConfig = JSON.parse(await readFile(path.join(config, 'plugin_config/obs-websocket/config.json')));
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const ws = new WebSocket(`ws://127.0.0.1:${wsConfig.server_port}`);
const pending = new Map();
let identified;
const ready = new Promise((resolve, reject) => {
  identified = resolve;
  ws.onerror = reject;
});
const hash = text => createHash('sha256').update(text).digest('base64');
ws.onmessage = ({ data }) => {
  const message = JSON.parse(data);
  if (message.op === 0) {
    const auth = message.d.authentication;
    const authentication = auth ? hash(hash(wsConfig.server_password + auth.salt) + auth.challenge) : undefined;
    ws.send(JSON.stringify({ op: 1, d: { rpcVersion: 1, authentication, eventSubscriptions: 0 } }));
  } else if (message.op === 2) {
    identified();
  } else if (message.op === 7) {
    const request = pending.get(message.d.requestId);
    pending.delete(message.d.requestId);
    if (message.d.requestStatus.result) request.resolve(message.d.responseData);
    else request.reject(new Error(JSON.stringify(message.d.requestStatus)));
  }
};
let serial = 0;
function request(requestType, requestData = {}) {
  return new Promise((resolve, reject) => {
    const requestId = String(++serial);
    const timeout = setTimeout(() => reject(new Error(`Timeout: ${requestType}`)), 10000);
    pending.set(requestId, {
      resolve: value => { clearTimeout(timeout); resolve(value); },
      reject: error => { clearTimeout(timeout); reject(error); },
    });
    ws.send(JSON.stringify({ op: 6, d: { requestType, requestData, requestId } }));
  });
}
const marker = color => request('TriggerHotkeyByName', { hotkeyName: `Polden.Marker${color}` });
const lines = async file => (await readFile(file, 'utf8')).trim().split(/\r?\n/).filter(Boolean);
const seconds = line => line.split(',')[0].split(':').reduce((sum, part) => sum * 60 + Number(part), 0);

try {
  await Promise.race([ready, delay(15000).then(() => { throw new Error('WebSocket startup timeout'); })]);
  const hotkeys = (await request('GetHotkeyList')).hotkeys;
  let previousJobs = [];
  try { previousJobs = JSON.parse(await readFile(path.join(config, 'polden-jobs.json'))); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  const previousIds = new Set(previousJobs.map(job => job.id));
  const previousCsvCount = (await readdir(path.join(runtime, 'recordings'))).filter(name => name.endsWith('.csv')).length;
  for (let color = 0; color < 8; ++color) assert(hotkeys.includes(`Polden.Marker${color}`));
  assert(!hotkeys.some(name => /infowriter/i.test(name)), 'Legacy plugin registered hotkeys');
  await marker(0);
  assert.equal((await readdir(path.join(runtime, 'recordings'))).filter(name => name.endsWith('.csv')).length, previousCsvCount);

  await request('StartRecord');
  await delay(1800);
  for (let color = 0; color < 8; ++color) { await marker(color); await delay(100); }
  await request('PauseRecord');
  await delay(300);
  const beforePause = (await request('GetRecordStatus')).outputTimecode;
  await marker(1);
  await delay(1500);
  await request('ResumeRecord');
  await delay(600);
  await marker(7);
  await request('SplitRecordFile');
  await delay(2400);
  await marker(6);
  const stopped = await request('StopRecord');
  assert(stopped.outputPath);

  let jobs;
  for (let attempt = 0; attempt < 100; ++attempt) {
    try { jobs = JSON.parse(await readFile(path.join(config, 'polden-jobs.json'))).filter(job => !previousIds.has(job.id)); }
    catch (error) { if (error.code === 'ENOENT') { await delay(300); continue; } throw error; }
    if (jobs.some(job => job.error)) throw new Error(JSON.stringify(jobs));
    if (jobs.length >= 2 && jobs.every(job => job.copied)) break;
    await delay(300);
  }
  assert.equal(jobs.length, 2, 'Expected two recording segments');
  assert(jobs.every(job => job.converted && job.copied), 'Conversion/delivery did not finish');
  const first = jobs[0];
  const second = jobs[1];
  for (const job of jobs) {
    const stem = path.parse(job.sourcePath).name;
    assert.equal(path.basename(job.localPath), `${stem}.mp4`);
    assert.equal(path.basename(job.destinationPath), `${stem}.mp4`);
    assert.equal(job.fileName, `${stem}.mp4`);
    const original = await readFile(path.join(path.dirname(job.sourcePath), `${stem}.csv`));
    for (const video of [job.localPath, job.destinationPath]) {
      assert.deepEqual(await readFile(path.join(path.dirname(video), `${stem}.csv`)), original);
    }
    assert.deepEqual(await readFile(job.localPath), await readFile(job.destinationPath));
  }
  const firstCsv = path.join(path.dirname(first.sourcePath), `${path.parse(first.sourcePath).name}.csv`);
  const secondCsv = path.join(path.dirname(second.sourcePath), `${path.parse(second.sourcePath).name}.csv`);
  const firstLines = await lines(firstCsv);
  assert.deepEqual(firstLines.map(line => Number(line.split(',')[1])), [0, 1, 2, 3, 4, 5, 6, 7, 7]);
  assert(seconds(firstLines[8]) - seconds(firstLines[7]) < 1.3, 'Pause was included in marker time');
  const secondLines = await lines(secondCsv);
  assert.equal(secondLines.length, 1);
  assert.equal(secondLines[0].split(',')[1], '6');
  assert(seconds(secondLines[0]) < 3, 'Split did not reset marker time');
  await marker(0);
  await delay(200);
  assert.deepEqual(await lines(secondCsv), secondLines, 'A marker was added after recording stopped');
  const settings = JSON.parse(await readFile(path.join(config, 'polden-markers.json')));
  for (let color = 0; color < 8; ++color) assert.equal(settings[`Polden.Marker${color}`].bindings.length, 1);
  console.log(JSON.stringify({ passed: true, colors: 8, segments: jobs.length,
    checks: ['all color IDs', 'inactive/paused guards', 'pause timing', 'split timing', 'original names',
      'local CSV', 'Lucid CSV', 'video checksums', 'shortcut persistence', 'legacy plugin exclusion'],
    beforePause, firstLines, secondLines }, null, 2));
} finally {
  ws.close();
}
