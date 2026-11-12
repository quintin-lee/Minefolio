#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "domain/skill/entity.h"
#include "domain/skill/rules.h"

int main(void) {
    printf("Testing domain/skill validation rules...\n");

    // Valid kebab-case name
    assert(mf_skill_validate_name("financial-analyzer") == 0);
    assert(mf_skill_validate_name("dca-advisor-v2") == 0);
    assert(mf_skill_validate_name("a") == 0);
    assert(mf_skill_validate_name("skill-1") == 0);

    // Invalid kebab-case names
    assert(mf_skill_validate_name("FinancialAnalyzer") != 0); // uppercase
    assert(mf_skill_validate_name("-leading-hyphen") != 0);
    assert(mf_skill_validate_name("trailing-hyphen-") != 0);
    assert(mf_skill_validate_name("consecutive--hyphen") != 0);
    assert(mf_skill_validate_name("") != 0);
    assert(mf_skill_validate_name(NULL) != 0);

    // Path traversal check
    assert(mf_skill_validate_exec_path("scripts/run.sh") == 0);
    assert(mf_skill_validate_exec_path("audit.py") == 0);
    assert(mf_skill_validate_exec_path("../etc/passwd") != 0);
    assert(mf_skill_validate_exec_path("/bin/sh") != 0);
    assert(mf_skill_validate_exec_path("scripts/../bad") != 0);
    assert(mf_skill_validate_exec_path("") == 0); // empty is valid (no script)

    // Full entity validation
    mf_skill_t sk = {0};
    strncpy(sk.name, "valid-skill", sizeof(sk.name) - 1);
    strncpy(sk.description, "A test description of what and when.", sizeof(sk.description) - 1);
    sk.instructions_l2 = "## Instructions\nDo something.";
    char err[256] = {0};
    assert(mf_skill_validate(&sk, err, sizeof(err)) == 0);

    // Entity validation failure: missing name
    mf_skill_t invalid_sk = {0};
    assert(mf_skill_validate(&invalid_sk, err, sizeof(err)) != 0);

    printf("test_domain_skill: ALL PASSED\n");
    return 0;
}
