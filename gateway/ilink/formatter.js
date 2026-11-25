'use strict';

function formatPushText(title, markdownBody) {
  const cleanTitle = String(title || '').trim();
  const lines = String(markdownBody || '')
    .replace(/\r\n?/g, '\n')
    .replace(/\[([^\]]+)\]\((https?:\/\/[^)]+)\)/g, '$1 ($2)')
    .replace(/```[^\n]*\n?/g, '')
    .replace(/`([^`]+)`/g, '$1')
    .replace(/\*\*([^*]+)\*\*/g, '$1')
    .replace(/__([^_]+)__/g, '$1')
    .split('\n');

  const outLines = [];
  for (let raw of lines) {
    if (/^\s*\|?\s*:?-{3,}:?(\s*\|\s*:?-{3,}:?)*\s*\|?\s*$/.test(raw)) {
      continue;
    }
    if (/^\s*-{3,}\s*$/.test(raw) || /^\s*\*{3,}\s*$/.test(raw)) {
      continue;
    }
    if (/^\s*\|.*\|\s*$/.test(raw)) {
      const cells = raw
        .trim()
        .replace(/^\|/, '')
        .replace(/\|$/, '')
        .split('|')
        .map((c) => c.trim())
        .filter(Boolean);
      raw = cells.join(' · ');
    } else {
      raw = raw.replace(/^\s*#{1,6}\s+/, '').replace(/^\s*>\s?/, '');
    }
    outLines.push(raw.replace(/[ \t]+$/g, ''));
  }

  let body = outLines.join('\n').replace(/\n{3,}/g, '\n\n').trim();
  if (cleanTitle && !body.startsWith(`【${cleanTitle}】`) && !body.startsWith(cleanTitle)) {
    body = body ? `【${cleanTitle}】\n\n${body}` : `【${cleanTitle}】`;
  }
  return body;
}

function splitPushChunks(text, chunkSize = Number(process.env.ILINK_REPLY_CHUNK || 1800)) {
  const s = String(text || '').trim();
  if (!s) return [];
  const parsed = Number(chunkSize);
  const max = Math.max(1, Number.isFinite(parsed) && parsed > 0 ? Math.floor(parsed) : 1800);
  const minSoftBreak = Math.floor(max * 0.6);
  const out = [];
  let pos = 0;

  while (pos < s.length) {
    if (pos + max >= s.length) {
      const tail = s.slice(pos).trim();
      if (tail) out.push(tail);
      break;
    }
    let cut = pos + max;
    const lastNl = s.lastIndexOf('\n', cut);
    if (lastNl >= pos + minSoftBreak) {
      cut = lastNl;
    }
    if (cut > pos && cut < s.length) {
      const prev = s.charCodeAt(cut - 1);
      const next = s.charCodeAt(cut);
      if (prev >= 0xd800 && prev <= 0xdbff && next >= 0xdc00 && next <= 0xdfff) {
        cut = cut - 1 > pos ? cut - 1 : cut + 1;
      }
    }
    const piece = s.slice(pos, cut).trim();
    if (piece) out.push(piece);
    pos = cut;
    while (pos < s.length && (s[pos] === '\n' || s[pos] === '\r')) {
      pos += 1;
    }
  }
  return out;
}

module.exports = {
  formatPushText,
  splitPushChunks,
};
