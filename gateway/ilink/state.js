'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

function validateInstanceId(id) {
  const s = String(id || '');
  if (!/^[1-9]\d*$/.test(s)) {
    throw new Error(`Invalid instance id: ${id}`);
  }
  return s;
}

function instanceDir(rootDir, id) {
  const safeId = validateInstanceId(id);
  const root = path.resolve(rootDir);
  const dir = path.resolve(root, safeId);
  if (path.dirname(dir) !== root) {
    throw new Error(`Unsafe instance directory: ${dir}`);
  }
  return dir;
}

function writeJsonAtomic(filePath, data) {
  const dir = path.dirname(filePath);
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  try {
    fs.chmodSync(dir, 0o700);
  } catch {
    // Ignore on platforms where chmod is restricted
  }
  const tmp = `${filePath}.tmp.${process.pid}.${crypto.randomBytes(4).toString('hex')}`;
  const content = `${JSON.stringify(data, null, 2)}\n`;
  const fd = fs.openSync(tmp, 'w', 0o600);
  try {
    fs.writeFileSync(fd, content, 'utf8');
    fs.fsyncSync(fd);
  } finally {
    fs.closeSync(fd);
  }
  fs.renameSync(tmp, filePath);
  try {
    fs.chmodSync(filePath, 0o600);
  } catch {
    // Ignore
  }
}

function readJson(filePath) {
  try {
    if (!fs.existsSync(filePath)) {
      return null;
    }
    const raw = fs.readFileSync(filePath, 'utf8');
    if (!raw.trim()) return null;
    return JSON.parse(raw);
  } catch {
    return null;
  }
}

function removeFile(filePath) {
  try {
    fs.unlinkSync(filePath);
  } catch (err) {
    if (err && err.code !== 'ENOENT') throw err;
  }
}

function removeInstanceDir(rootDir, id) {
  const dir = instanceDir(rootDir, id);
  fs.rmSync(dir, { recursive: true, force: true });
}

function listStateDirs(rootDir) {
  const root = path.resolve(rootDir);
  if (!fs.existsSync(root)) return [];
  const entries = fs.readdirSync(root, { withFileTypes: true });
  return entries
    .filter((e) => e.isDirectory() && /^[1-9]\d*$/.test(e.name))
    .map((e) => Number(e.name));
}

function createStateStore({ rootDir, instanceId }) {
  const dir = instanceDir(rootDir, instanceId);
  const accountPath = path.join(dir, 'account.json');
  const cursorPath = path.join(dir, 'cursor.json');
  const dedupPath = path.join(dir, 'dedup.json');

  return {
    dir,
    readAccount: () => readJson(accountPath),
    writeAccount: (acc) => writeJsonAtomic(accountPath, acc),
    clearAccount: () => removeFile(accountPath),
    readCursor: () => readJson(cursorPath),
    writeCursor: (cur) => writeJsonAtomic(cursorPath, cur),
    readDedup: () => readJson(dedupPath),
    writeDedup: (d) => writeJsonAtomic(dedupPath, d),
  };
}

module.exports = {
  instanceDir,
  writeJsonAtomic,
  readJson,
  removeInstanceDir,
  listStateDirs,
  createStateStore,
};
