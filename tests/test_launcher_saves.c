#include "launcher_saves.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", \
    __FILE__, __LINE__, #x, SDL_GetError()); exit(1); } } while (0)

static unsigned char original[2048], imported[2048];
static void CheckFile(const char *path, const unsigned char *expected) {
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    CHECK(data && size == 2048 && !SDL_memcmp(data, expected, size));
    SDL_free(data);
}

int main(int argc, char **argv) {
    char error[1024];
    /* CTest provides an isolated working directory. Remove only known test
     * artifacts so repeated runs still exercise a missing saves directory. */
    SDL_RemovePath("saves/save.srm");
    SDL_RemovePath("saves/save.srm.bak");
    SDL_RemovePath("saves/save.srm.importing");
    SDL_RemovePath("saves");
    SDL_PathInfo info;
    CHECK(!SDL_GetPathInfo("saves", &info));
    for (int i = 0; i < 2048; ++i) {
        original[i] = (unsigned char)i;
        imported[i] = (unsigned char)(i * 7 + 3);
    }
    const char *source = "download-\xc3\xa9.srm";
    CHECK(SDL_SaveFile(source, original, sizeof(original)));
    CHECK(FZeroImportSave(source, error, sizeof(error)) && !error[0]);
    CheckFile("saves/save.srm", original);
    CheckFile(source, original);

    CHECK(SDL_SaveFile(source, imported, sizeof(imported)));
    CHECK(FZeroImportSave(source, error, sizeof(error)));
    CheckFile("saves/save.srm", imported);
    CheckFile("saves/save.srm.bak", original);
    /* Importing the backup must read it before the backup gets replaced. */
    CHECK(FZeroImportSave("saves/save.srm.bak", error, sizeof(error)));
    CheckFile("saves/save.srm", original);
    CheckFile("saves/save.srm.bak", imported);
    CHECK(FZeroImportSave("saves/../saves/save.srm", error, sizeof(error)));
    CheckFile("saves/save.srm", original);

    CHECK(!FZeroImportSave("missing.srm", error, sizeof(error)) && error[0]);
    CHECK(SDL_SaveFile("empty.srm", "", 0));
    CHECK(!FZeroImportSave("empty.srm", error, sizeof(error)) && error[0]);
    CheckFile("saves/save.srm", original);
    /* A failed backup must prevent both replacement and clearing. */
    CHECK(SDL_RemovePath("saves/save.srm.bak"));
    CHECK(SDL_CreateDirectory("saves/save.srm.bak"));
    CHECK(!FZeroImportSave(source, error, sizeof(error)) && error[0]);
    CHECK(!FZeroClearSave(error, sizeof(error)) && error[0]);
    CheckFile("saves/save.srm", original);
    CHECK(SDL_RemovePath("saves/save.srm.bak"));
    /* Do not overwrite an existing temporary import. */
    CHECK(SDL_SaveFile("saves/save.srm.importing", imported, sizeof(imported)));
    CHECK(!FZeroImportSave(source, error, sizeof(error)) && error[0]);
    CheckFile("saves/save.srm", original);
    CheckFile("saves/save.srm.importing", imported);
    CHECK(SDL_RemovePath("saves/save.srm.importing"));
    CHECK(FZeroClearSave(error, sizeof(error)));
    CHECK(!SDL_GetPathInfo("saves/save.srm", &info));
    CheckFile("saves/save.srm.bak", original);
    /* Optionally verify a user's file, read-only, in this isolated directory. */
    if (argc == 2) {
        size_t size = 0, actual_size = 0;
        void *expected = SDL_LoadFile(argv[1], &size);
        CHECK(expected);
        CHECK(FZeroImportSave(argv[1], error, sizeof(error)));
        void *actual = SDL_LoadFile("saves/save.srm", &actual_size);
        CHECK(actual && size == actual_size && !SDL_memcmp(expected, actual, size));
        SDL_free(expected); SDL_free(actual);
    }
    puts("launcher save tests: passed");
    return 0;
}
