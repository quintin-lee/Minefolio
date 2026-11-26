'use strict';

const { test, describe } = require('node:test');
const assert = require('node:assert');

const { parseArgs, pickSessionError } = require('../runner');
const { IlinkSessionExpiredError } = require('../client');

describe('ilink/runner argument parsing', () => {
  test('defaults to no instance and no forced login', () => {
    assert.deepStrictEqual(parseArgs([]), { instanceId: null, forceLogin: false });
  });

  test('ignores unknown flags and a trailing --instance without a value', () => {
    assert.deepStrictEqual(parseArgs(['--unknown', '--force-login', '--instance']), {
      instanceId: null,
      forceLogin: true,
    });
  });

  test('uses the last --instance value when repeated', () => {
    assert.deepStrictEqual(parseArgs(['--instance', '3', '--instance', '7']), {
      instanceId: '7',
      forceLogin: false,
    });
  });
});

describe('ilink/runner session error selection', () => {
  test('returns null when every worker settled successfully', () => {
    assert.strictEqual(pickSessionError([{ status: 'fulfilled', value: 1 }], []), null);
  });

  test('prefers an expired-session error over unrelated worker errors', () => {
    const generic = new Error('monitor failed');
    const expired = new IlinkSessionExpiredError('session expired');
    const selected = pickSessionError(
      [{ status: 'rejected', reason: generic }],
      [generic, expired]
    );
    assert.strictEqual(selected, expired);
  });

  test('deduplicates an error reported by both a settled result and a worker', () => {
    const err = new Error('shared failure');
    const selected = pickSessionError([{ status: 'rejected', reason: err }], [err]);
    assert.strictEqual(selected, err);
  });
});
