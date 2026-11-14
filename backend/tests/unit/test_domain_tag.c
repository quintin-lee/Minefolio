#include "domain/tag/rules.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Starting Domain Tag Rules Tests ===\n");

    // 1. validate_name
    assert(!mf_tag_rule_validate_name(NULL));
    assert(!mf_tag_rule_validate_name(""));
    assert(mf_tag_rule_validate_name("餐饮"));
    assert(mf_tag_rule_validate_name("Travel-2026"));

    char long_name[256];
    memset(long_name, 't', sizeof(long_name));
    long_name[128] = '\0';
    assert(mf_tag_rule_validate_name(long_name));
    long_name[128] = 't';
    long_name[129] = '\0';
    assert(!mf_tag_rule_validate_name(long_name));
    printf("  [OK] mf_tag_rule_validate_name\n");

    // 2. validate_color
    assert(mf_tag_rule_validate_color(NULL));  // optional -> true
    assert(mf_tag_rule_validate_color(""));    // optional -> true
    assert(mf_tag_rule_validate_color("#1890ff"));
    assert(mf_tag_rule_validate_color("#FFFFFF"));
    assert(mf_tag_rule_validate_color("#000000"));
    assert(!mf_tag_rule_validate_color("1890ff"));  // missing #
    assert(!mf_tag_rule_validate_color("#1890"));   // too short
    assert(!mf_tag_rule_validate_color("#1890ffff")); // too long
    printf("  [OK] mf_tag_rule_validate_color\n");

    printf("=== Domain Tag Rules Tests Passed ===\n");
    return 0;
}
