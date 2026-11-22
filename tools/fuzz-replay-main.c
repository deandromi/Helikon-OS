/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Standalone driver for a fuzz harness, for hosts without libFuzzer.
 *
 *   <driver> FILE...
 *
 * Reads each file, hands its bytes to `LLVMFuzzerTestOneInput`, and stops at the
 * first harness abort. `tools/fuzz.sh --replay` builds each harness against this
 * file and walks the committed corpus, so the same inputs that libFuzzer keeps
 * are a regression suite on a machine whose clang has no fuzzer runtime — which
 * includes every Mac with only the Command Line Tools installed.
 */
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const unsigned char *data, unsigned long size);

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s FILE...\n", argv[0]);
        return 2;
    }
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        FILE *file = fopen(argv[i], "rb");
        if (file == NULL) {
            fprintf(stderr, "replay: cannot open %s\n", argv[i]);
            ++failures;
            continue;
        }
        if (fseek(file, 0, SEEK_END) != 0) {
            fprintf(stderr, "replay: cannot seek %s\n", argv[i]);
            fclose(file);
            ++failures;
            continue;
        }
        long length = ftell(file);
        if (length < 0 || length > (long)(16 * 1024 * 1024)) {
            fprintf(stderr, "replay: %s is too large\n", argv[i]);
            fclose(file);
            ++failures;
            continue;
        }
        rewind(file);
        unsigned char *buffer = malloc((size_t)length + 1u);
        if (buffer == NULL) {
            fprintf(stderr, "replay: out of memory\n");
            fclose(file);
            return 1;
        }
        size_t got = fread(buffer, 1u, (size_t)length, file);
        fclose(file);
        if (got != (size_t)length) {
            fprintf(stderr, "replay: short read on %s\n", argv[i]);
            free(buffer);
            ++failures;
            continue;
        }
        LLVMFuzzerTestOneInput(buffer, got);
        free(buffer);
    }
    if (failures != 0) {
        fprintf(stderr, "replay: %d file(s) failed\n", failures);
        return 1;
    }
    return 0;
}
