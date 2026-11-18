/**
 * Test/mock helpers for the OneBot adapter.
 *
 * createMockTransport() records every sendPrivateMsg call so a test can
 * assert the reply without touching a real IM runtime. makePrivateEvent()
 * builds a OneBot v11 private-message event.
 */

function createMockTransport() {
  const sent = [];
  return {
    sent,
    async sendPrivateMsg(uid, text) {
      sent.push({ uid, text });
    },
  };
}

/** Build a OneBot v11 private-message event (the subset the adapter reads). */
function makePrivateEvent({ userId, text, extra }) {
  return Object.assign(
    {
      post_type: 'message',
      message_type: 'private',
      raw_message: text,
      sender: { user_id: userId },
    },
    extra || {},
  );
}

/** Build a group-message event (must be ignored by the P1 adapter). */
function makeGroupEvent({ groupId, userId, text }) {
  return {
    post_type: 'message',
    message_type: 'group',
    raw_message: text,
    group_id: groupId,
    sender: { user_id: userId },
  };
}

module.exports = { createMockTransport, makePrivateEvent, makeGroupEvent };
