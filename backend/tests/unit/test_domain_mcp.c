#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include "domain/mcp/rules.h"

static void test_validate_name(void)
{
    assert(mf_mcp_rule_validate_name("github") == true);
    assert(mf_mcp_rule_validate_name("notion-api") == true);
    assert(mf_mcp_rule_validate_name("slack_2fa") == true);
    assert(mf_mcp_rule_validate_name(NULL) == false);
    assert(mf_mcp_rule_validate_name("") == false);
    assert(mf_mcp_rule_validate_name("GitHub") == false);     /* uppercase */
    assert(mf_mcp_rule_validate_name("my server") == false);   /* space */
    assert(mf_mcp_rule_validate_name("a;b") == false);         /* shell meta */

    /* 64 chars allowed, 65 rejected */
    char name64[65], name65[66];
    memset(name64, 'a', 64);
    name64[64] = '\0';
    memset(name65, 'a', 65);
    name65[65] = '\0';
    assert(mf_mcp_rule_validate_name(name64) == true);
    assert(mf_mcp_rule_validate_name(name65) == false);

    printf("PASS: test_validate_name\n");
}

static void test_validate_transport(void)
{
    assert(mf_mcp_rule_validate_transport("http") == true);
    assert(mf_mcp_rule_validate_transport("stdio") == true);
    assert(mf_mcp_rule_validate_transport(NULL) == false);
    assert(mf_mcp_rule_validate_transport("") == false);
    assert(mf_mcp_rule_validate_transport("sse") == false);
    assert(mf_mcp_rule_validate_transport("httpx") == false);

    printf("PASS: test_validate_transport\n");
}

static void test_validate_url(void)
{
    /* valid public https URL */
    assert(mf_mcp_rule_validate_url("https://api.github.com/mcp", false) == true);
    assert(mf_mcp_rule_validate_url("http://example.com/mcp", false) == true);

    /* bad schemes */
    assert(mf_mcp_rule_validate_url("ftp://example.com", false) == false);
    assert(mf_mcp_rule_validate_url("not-a-url", false) == false);
    assert(mf_mcp_rule_validate_url(NULL, false) == false);
    assert(mf_mcp_rule_validate_url("", false) == false);

    /* loopback / private ranges blocked when allow_local=false */
    assert(mf_mcp_rule_validate_url("https://localhost/mcp", false) == false);
    assert(mf_mcp_rule_validate_url("http://127.0.0.1:3000", false) == false);
    assert(mf_mcp_rule_validate_url("http://192.168.1.50:8080", false) == false);
    assert(mf_mcp_rule_validate_url("http://10.0.0.1:9000", false) == false);
    assert(mf_mcp_rule_validate_url("http://172.16.0.1:80", false) == false);
    assert(mf_mcp_rule_validate_url("http://0.0.0.0:80", false) == false);
    assert(mf_mcp_rule_validate_url("http://::1", false) == false);

    /* same local addresses allowed when allow_local=true */
    assert(mf_mcp_rule_validate_url("http://localhost:3000", true) == true);
    assert(mf_mcp_rule_validate_url("http://127.0.0.1:3000", true) == true);
    assert(mf_mcp_rule_validate_url("http://192.168.1.50:8080", true) == true);

    printf("PASS: test_validate_url\n");
}

static void test_validate_command(void)
{
    assert(mf_mcp_rule_validate_command("/usr/bin/npx") == true);
    assert(mf_mcp_rule_validate_command("npx") == true);
    assert(mf_mcp_rule_validate_command("/usr/bin/node /path/server.js") == true);
    assert(mf_mcp_rule_validate_command(NULL) == false);
    assert(mf_mcp_rule_validate_command("") == false);

    /* shell metacharacters blocked */
    assert(mf_mcp_rule_validate_command("npx; rm -rf /") == false);
    assert(mf_mcp_rule_validate_command("npx && evil") == false);
    assert(mf_mcp_rule_validate_command("npx `whoami`") == false);
    assert(mf_mcp_rule_validate_command("npx $(id)") == false);
    assert(mf_mcp_rule_validate_command("npx | grep x") == false);
    assert(mf_mcp_rule_validate_command("npx > out.txt") == false);
    assert(mf_mcp_rule_validate_command("npx < in.txt") == false);

    /* length > 512 blocked */
    char long_cmd[600];
    memset(long_cmd, 'a', 599);
    long_cmd[599] = '\0';
    assert(mf_mcp_rule_validate_command(long_cmd) == false);

    printf("PASS: test_validate_command\n");
}

static void test_validate_timeout_ms(void)
{
    assert(mf_mcp_rule_validate_timeout_ms(1000) == true);
    assert(mf_mcp_rule_validate_timeout_ms(30000) == true);
    assert(mf_mcp_rule_validate_timeout_ms(120000) == true);
    assert(mf_mcp_rule_validate_timeout_ms(999) == false);
    assert(mf_mcp_rule_validate_timeout_ms(0) == false);
    assert(mf_mcp_rule_validate_timeout_ms(-1) == false);
    assert(mf_mcp_rule_validate_timeout_ms(120001) == false);
    assert(mf_mcp_rule_validate_timeout_ms(INT_MAX) == false);

    printf("PASS: test_validate_timeout_ms\n");
}

int main(void)
{
    test_validate_name();
    test_validate_transport();
    test_validate_url();
    test_validate_command();
    test_validate_timeout_ms();
    printf("All domain mcp tests passed successfully!\n");
    return 0;
}
