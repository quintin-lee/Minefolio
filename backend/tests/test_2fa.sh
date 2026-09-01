#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKEND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$BACKEND_DIR"

PORT=8092
SERVER_PID=""
DB_PATH="/tmp/test_2fa_$$.db"

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$DB_PATH"
}
trap cleanup EXIT INT TERM

echo "=== 1. Starting Minefolio server on port $PORT ==="
MINEFOLIO_PORT=$PORT MINEFOLIO_DB_DRIVER=sqlite MINEFOLIO_DB_DSN="$DB_PATH" MINEFOLIO_JWT_SECRET="test_2fa_secret_1234567890123456" ./build/minefolio &
SERVER_PID=$!

# Wait for server to start
for i in {1..30}; do
    if curl -s "http://127.0.0.1:$PORT/api/system/status" >/dev/null 2>&1; then
        break
    fi
    sleep 0.2
done

rsa_encrypt() {
  local plain="$1"
  node -e "
const http = require('http');
http.get('http://127.0.0.1:$PORT/api/auth/public-key', res => {
  let d = '';
  res.on('data', c => d += c);
  res.on('end', async () => {
    const outer = JSON.parse(d);
    const jwk = typeof outer.data.public_key === 'string'
      ? JSON.parse(outer.data.public_key) : outer.data.public_key;
    const key = await crypto.subtle.importKey('jwk', jwk,
      {name:'RSA-OAEP', hash:'SHA-256'}, false, ['encrypt']);
    const enc = await crypto.subtle.encrypt({name:'RSA-OAEP'}, key, Buffer.from('$plain'));
    const arr = new Uint8Array(enc);
    process.stdout.write(btoa(String.fromCharCode(...arr))
      .replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,''));
  });
});"
}

echo "=== 2. Setup System / Register User ==="
SETUP_ENC=$(rsa_encrypt "password123")
SETUP_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/system/setup" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password_enc\":\"$SETUP_ENC\"}")
echo "Setup: $SETUP_RES"
TOKEN=$(echo "$SETUP_RES" | grep -o '"token":"[^"]*' | cut -d'"' -f4)
if [ -z "$TOKEN" ]; then
    echo "Failed to get token"
    exit 1
fi

echo "=== 3. Check Initial 2FA Status ==="
STATUS_RES=$(curl -s -X GET "http://127.0.0.1:$PORT/api/auth/2fa/status" \
    -H "Authorization: Bearer $TOKEN")
echo "Status: $STATUS_RES"
if ! echo "$STATUS_RES" | grep -q '"enabled":false'; then
    echo "Expected 2FA to be disabled initially"
    exit 1
fi

echo "=== 4. 2FA Setup (Generate Secret) ==="
SETUP_2FA=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/2fa/setup" \
    -H "Authorization: Bearer $TOKEN")
echo "2FA Setup: $SETUP_2FA"
SECRET=$(echo "$SETUP_2FA" | grep -o '"secret":"[^"]*' | cut -d'"' -f4)
if [ -z "$SECRET" ]; then
    echo "Failed to get 2FA secret"
    exit 1
fi

echo "=== 5. Try Enabling 2FA with Invalid Code ==="
INVALID_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/2fa/enable" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"code":"000000"}')
echo "Invalid enable: $INVALID_RES"
if echo "$INVALID_RES" | grep -q '"code":0'; then
    echo "Should not enable 2FA with invalid code"
    exit 1
fi

echo "=== 6. Generate Valid TOTP Code via Python ==="
VALID_CODE=$(python3 -c "
import hmac, hashlib, time, struct, base64

secret = '$SECRET'
key = base64.b32decode(secret, casefold=True)
counter = int(time.time()) // 30
msg = struct.pack('>Q', counter)
h = hmac.new(key, msg, hashlib.sha1).digest()
offset = h[-1] & 0x0F
binary = struct.unpack('>I', h[offset:offset+4])[0] & 0x7fffffff
otp = binary % 1000000
print(f'{otp:06d}')
")
echo "Calculated TOTP code: $VALID_CODE"

echo "=== 7. Enable 2FA with Valid Code ==="
ENABLE_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/2fa/enable" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"code\":\"$VALID_CODE\"}")
echo "Enable res: $ENABLE_RES"
if ! echo "$ENABLE_RES" | grep -q '"backup_codes":\['; then
    echo "Failed to enable 2FA"
    exit 1
fi

BACKUP_CODE_1=$(echo "$ENABLE_RES" | grep -o '"[0-9a-z]\{4\}-[0-9a-z]\{4\}"' | head -n 1 | tr -d '"')
echo "Extracted first backup code: $BACKUP_CODE_1"

echo "=== 8. Check 2FA Status after Enable ==="
STATUS_ENABLED=$(curl -s -X GET "http://127.0.0.1:$PORT/api/auth/2fa/status" \
    -H "Authorization: Bearer $TOKEN")
echo "Status enabled: $STATUS_ENABLED"
if ! echo "$STATUS_ENABLED" | grep -q '"enabled":true'; then
    echo "Expected 2FA to be enabled"
    exit 1
fi

echo "=== 9. Login Without 2FA (Should prompt require_2fa) ==="
LOGIN_ENC=$(rsa_encrypt "password123")
LOGIN_PROMPT=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password_enc\":\"$LOGIN_ENC\"}")
echo "Login prompt: $LOGIN_PROMPT"
if ! echo "$LOGIN_PROMPT" | grep -q '"require_2fa":true'; then
    echo "Expected require_2fa in login response"
    exit 1
fi
TEMP_TOKEN=$(echo "$LOGIN_PROMPT" | grep -o '"temp_token":"[^"]*' | cut -d'"' -f4)

echo "=== 10. Complete 2FA Login with Valid Code ==="
CURRENT_CODE=$(python3 -c "
import hmac, hashlib, time, struct, base64
secret = '$SECRET'
key = base64.b32decode(secret, casefold=True)
counter = int(time.time()) // 30
msg = struct.pack('>Q', counter)
h = hmac.new(key, msg, hashlib.sha1).digest()
offset = h[-1] & 0x0F
binary = struct.unpack('>I', h[offset:offset+4])[0] & 0x7fffffff
otp = binary % 1000000
print(f'{otp:06d}')
")
VERIFY_LOGIN=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/2fa/verify-login" \
    -H "Content-Type: application/json" \
    -d "{\"temp_token\":\"$TEMP_TOKEN\",\"code\":\"$CURRENT_CODE\"}")
echo "Verify login: $VERIFY_LOGIN"
NEW_TOKEN=$(echo "$VERIFY_LOGIN" | grep -o '"token":"[^"]*' | cut -d'"' -f4)
if [ -z "$NEW_TOKEN" ]; then
    echo "Failed to complete 2FA login"
    exit 1
fi

echo "=== 11. Test Using Backup Code on 2FA Login ==="
LOGIN_ENC_2=$(rsa_encrypt "password123")
LOGIN_PROMPT_2=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password_enc\":\"$LOGIN_ENC_2\"}")
TEMP_TOKEN_2=$(echo "$LOGIN_PROMPT_2" | grep -o '"temp_token":"[^"]*' | cut -d'"' -f4)
BACKUP_LOGIN=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/2fa/verify-login" \
    -H "Content-Type: application/json" \
    -d "{\"temp_token\":\"$TEMP_TOKEN_2\",\"code\":\"$BACKUP_CODE_1\"}")
echo "Backup code login: $BACKUP_LOGIN"
if ! echo "$BACKUP_LOGIN" | grep -q '"token":"'; then
    echo "Failed to login with backup code"
    exit 1
fi

echo "=== 12. Test Reusing Same Backup Code (Should Fail) ==="
LOGIN_ENC_3=$(rsa_encrypt "password123")
LOGIN_PROMPT_3=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password_enc\":\"$LOGIN_ENC_3\"}")
TEMP_TOKEN_3=$(echo "$LOGIN_PROMPT_3" | grep -o '"temp_token":"[^"]*' | cut -d'"' -f4)
REUSED_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/2fa/verify-login" \
    -H "Content-Type: application/json" \
    -d "{\"temp_token\":\"$TEMP_TOKEN_3\",\"code\":\"$BACKUP_CODE_1\"}")
echo "Reused backup code res: $REUSED_RES"
if echo "$REUSED_RES" | grep -q '"token":"'; then
    echo "Backup code was not consumed (reused successfully!)"
    exit 1
fi

echo "=== 13. Disable 2FA ==="
DISABLE_RES=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/2fa/disable" \
    -H "Authorization: Bearer $NEW_TOKEN")
echo "Disable res: $DISABLE_RES"

STATUS_DISABLED=$(curl -s -X GET "http://127.0.0.1:$PORT/api/auth/2fa/status" \
    -H "Authorization: Bearer $NEW_TOKEN")
echo "Status disabled: $STATUS_DISABLED"
if ! echo "$STATUS_DISABLED" | grep -q '"enabled":false'; then
    echo "Expected 2FA to be disabled"
    exit 1
fi

echo "=== 14. Direct Login after 2FA Disabled (Should return token directly) ==="
LOGIN_ENC_4=$(rsa_encrypt "password123")
DIRECT_LOGIN=$(curl -s -X POST "http://127.0.0.1:$PORT/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"admin\",\"password_enc\":\"$LOGIN_ENC_4\"}")
echo "Direct login: $DIRECT_LOGIN"
if ! echo "$DIRECT_LOGIN" | grep -q '"token":"'; then
    echo "Failed to login directly after disabling 2FA"
    exit 1
fi

echo "=== ALL 2FA INTEGRATION TESTS PASSED SUCCESSFULLY! ==="
