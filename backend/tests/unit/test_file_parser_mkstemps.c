#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "services/file_parser.h"

int main(void) {
    printf("Running test_file_parser_mkstemps...\n");

    const char* sample_data = "%PDF-1.4 sample content";
    char out_path[256];

    /* 1. Test .pdf suffix */
    int rc = file_parser_write_temp(sample_data, strlen(sample_data), ".pdf", out_path, sizeof(out_path));
    assert(rc == 0);
    assert(strstr(out_path, ".pdf") != NULL);
    assert(access(out_path, R_OK) == 0);
    unlink(out_path);

    /* 2. Test .zip suffix */
    rc = file_parser_write_temp("PK\x03\x04", 4, ".zip", out_path, sizeof(out_path));
    assert(rc == 0);
    assert(strstr(out_path, ".zip") != NULL);
    assert(access(out_path, R_OK) == 0);
    unlink(out_path);

    /* 3. Negative tests: invalid suffix with path traversal or injection */
    assert(file_parser_write_temp(sample_data, 10, "../../etc/passwd", out_path, sizeof(out_path)) == -1);
    assert(file_parser_write_temp(sample_data, 10, ".p;d'f", out_path, sizeof(out_path)) == -1);
    assert(file_parser_write_temp(sample_data, 10, ".toolong_extension_overflow_check", out_path, sizeof(out_path)) == -1);

    printf("PASS: test_file_parser_mkstemps completed successfully\n");
    return 0;
}
