/* Locks the B6 manifest hash: native file_hash MUST equal the Gradle
 * computation (FNV-1a 64, basis 0xcbf29ce484222325). A basis typo here means
 * every correctly-copied file is rejected as a hash mismatch on fresh
 * install. Vectors: empty file and "hello\n". */
#include <stdio.h>
#include <string.h>

/* Copied verbatim from android_main.c (keep in sync). */
static void file_hash(const char *path, char out[17])
{
    FILE *f = fopen(path, "rb");
    unsigned long long h = 14695981039346656037ULL;
    unsigned char buf[16384];
    size_t n;

    out[0] = '\0';
    if (f == NULL) {
        return;
    }
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        size_t i;
        for (i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
    }
    fclose(f);
    snprintf(out, 17, "%016llx", h);
}

int main(void)
{
    int fails = 0;
    char got[17];

    file_hash("build/probe/empty.bin", got);
    if (strcmp(got, "cbf29ce484222325") != 0) {
        printf("FAIL: empty -> %s, want cbf29ce484222325\n", got);
        fails++;
    }
    file_hash("build/probe/t.txt", got);
    if (strcmp(got, "a9bc80cca21f28b3") != 0) {
        printf("FAIL: hello -> %s, want a9bc80cca21f28b3\n", got);
        fails++;
    }
    if (fails == 0) {
        printf("ok: native FNV matches Gradle vectors\n");
    }
    return fails != 0;
}
