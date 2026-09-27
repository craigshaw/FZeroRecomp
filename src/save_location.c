#include "save_location.h"
#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define FOLDER_CONFIG "config.ini"
#define FOLDER_TEMP "config.ini.save-folder.tmp"
/* Keep the complete Folder line below config.c's 4096-byte line limit. */
#define FOLDER_CAPACITY 4000

static char folder[FOLDER_CAPACITY] = "saves";
static char save_file[FZERO_SAVE_PATH_CAPACITY] = "saves/save.srm";
static bool is_default = true;

static bool Absolute(const char *path) {
    if (path[0] == '/') return true;
#ifdef _WIN32
    if (path[0] == '\\' && path[1] == '\\') return true;
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && (path[2] == '\\' || path[2] == '/')) return true;
#endif
    return false;
}

static bool Set(const char *selected, char *error, size_t capacity) {
    const char *next = selected[0] ? selected : "saves";
    size_t length = strlen(next);
    if (length >= sizeof(folder) || strchr(next, '\n') || strchr(next, '\r') ||
        (selected[0] && !Absolute(selected))) {
        SDL_snprintf(error, capacity, "Select an absolute folder path without line breaks.");
        return false;
    }
    char resolved[sizeof(save_file)];
    int count = SDL_snprintf(resolved, sizeof(resolved), "%s%ssave.srm", next,
        length && (next[length - 1] == '/' || next[length - 1] == '\\') ? "" : "/");
    if (count < 0 || (size_t)count >= sizeof(resolved)) {
        SDL_snprintf(error, capacity, "The save folder path is too long.");
        return false;
    }
    SDL_strlcpy(folder, next, sizeof(folder));
    SDL_strlcpy(save_file, resolved, sizeof(save_file));
    is_default = !selected[0];
    return true;
}

static char *SkipSpace(char *line) {
    while (*line == ' ' || *line == '\t') ++line;
    return line;
}

static int Section(char *line) {
    char *p = SkipSpace(line);
    if (*p != '[') return 0;
    char *end = strchr(++p, ']');
    if (!end) return 0;
    *end = 0;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    return SDL_strcasecmp(p, "Save") == 0 ? 1 : 2;
}

static char *FolderValue(char *line) {
    char *p = SkipSpace(line);
    char *eq = strchr(p, '=');
    if (!eq || *p == '#' || *p == ';') return NULL;
    char *end = eq;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    if ((size_t)(end - p) != 6 || SDL_strncasecmp(p, "Folder", 6)) return NULL;
    p = SkipSpace(eq + 1);
    end = p + strlen(p);
    while (end > p && (end[-1] == '\n' || end[-1] == '\r')) *--end = 0;
    return p;
}

static bool CompleteLine(const char *line, FILE *in) {
    return strchr(line, '\n') != NULL || feof(in);
}

bool FZeroSaveLocationLoad(char *error, size_t capacity) {
    error[0] = 0;
    FILE *in = fopen(FOLDER_CONFIG, "rb");
    if (!in) {
        if (errno == ENOENT) return Set("", error, capacity);
        SDL_snprintf(error, capacity, "Cannot read save folder setting: %s", strerror(errno));
        return false;
    }
    char raw[8192], copy[8192], selected[FOLDER_CAPACITY] = "";
    bool in_save = false, ok = true;
    while (fgets(raw, sizeof(raw), in)) {
        if (!CompleteLine(raw, in)) { ok = false; break; }
        SDL_strlcpy(copy, raw, sizeof(copy));
        int section = Section(copy);
        if (section) { in_save = section == 1; continue; }
        if (!in_save) continue;
        char *value = FolderValue(copy);
        if (value) {
            if (strlen(value) >= sizeof(selected)) { ok = false; break; }
            SDL_strlcpy(selected, value, sizeof(selected));
        }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (ok) ok = Set(selected, error, capacity);
    if (!ok && !error[0]) SDL_snprintf(error, capacity, "Save folder setting in config.ini is invalid.");
    return ok;
}

static bool WriteFolder(FILE *out, const char *selected) {
    return fprintf(out, "Folder = %s\n", selected) >= 0;
}

static bool Persist(const char *selected, char *error, size_t capacity) {
    FILE *in = fopen(FOLDER_CONFIG, "rb");
    if (!in && errno != ENOENT) {
        SDL_snprintf(error, capacity, "Cannot read config.ini: %s", strerror(errno));
        return false;
    }
    FILE *out = fopen(FOLDER_TEMP, "wb");
    if (!out) {
        if (in) fclose(in);
        SDL_snprintf(error, capacity, "Cannot stage config.ini: %s", strerror(errno));
        return false;
    }
    char raw[8192], copy[8192];
    bool in_save = false, found_save = false, wrote = false, ok = true;
    if (in) {
        while (fgets(raw, sizeof(raw), in)) {
            if (!CompleteLine(raw, in)) { ok = false; break; }
            SDL_strlcpy(copy, raw, sizeof(copy));
            int section = Section(copy);
            if (section) {
                if (in_save && !wrote) { ok = WriteFolder(out, selected); wrote = true; }
                in_save = section == 1;
                found_save |= in_save;
            } else if (in_save && FolderValue(copy)) {
                if (!wrote) ok = WriteFolder(out, selected);
                wrote = true;
                if (!ok) break;
                continue;
            }
            if (fputs(raw, out) == EOF) { ok = false; break; }
            if (*raw && raw[strlen(raw) - 1] != '\n' && fputc('\n', out) == EOF) { ok = false; break; }
        }
        if (ferror(in)) ok = false;
        fclose(in);
    }
    if (ok && in_save && !wrote) ok = WriteFolder(out, selected);
    if (ok && !found_save) ok = fputs("\n[Save]\n", out) != EOF && WriteFolder(out, selected);
    if (fclose(out)) ok = false;
    if (ok) ok = SDL_RenamePath(FOLDER_TEMP, FOLDER_CONFIG);
    if (!ok) {
        SDL_RemovePath(FOLDER_TEMP);
        SDL_snprintf(error, capacity, "Cannot update config.ini: %s", SDL_GetError());
    }
    return ok;
}

bool FZeroSaveLocationSelect(const char *selected, char *error, size_t capacity) {
    error[0] = 0;
    if (!selected) selected = "";
    char previous[FOLDER_CAPACITY];
    SDL_strlcpy(previous, is_default ? "" : folder, sizeof(previous));
    if (!Set(selected, error, capacity)) return false;
    if (selected[0]) {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(folder, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
            SDL_snprintf(error, capacity, "The selected save folder is unavailable or is not a folder.");
            Set(previous, error, capacity);
            return false;
        }
    }
    if (Persist(selected, error, capacity)) return true;
    Set(previous, error, capacity);
    return false;
}

const char *FZeroSaveDirectory(void) { return folder; }
const char *FZeroSaveFile(void) { return save_file; }
bool FZeroSaveIsDefault(void) { return is_default; }

bool FZeroSavePath(char *out, size_t capacity, const char *name) {
    size_t length = strlen(folder);
    int count = SDL_snprintf(out, capacity, "%s%s%s", folder,
        length && (folder[length - 1] == '/' || folder[length - 1] == '\\') ? "" : "/", name);
    return count >= 0 && (size_t)count < capacity;
}

bool FZeroSaveDirectoryAvailable(void) {
    if (is_default) return true;
    SDL_PathInfo info;
    return SDL_GetPathInfo(folder, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

bool FZeroSaveEnsureDirectory(void) {
    return is_default ? SDL_CreateDirectory(folder) : FZeroSaveDirectoryAvailable();
}

FZeroSavePathState FZeroSavePathStatus(const char *path) {
#ifdef _WIN32
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info)) return FZERO_SAVE_PATH_EXISTS;
    /* SDL uses UTF-8 paths. Query Win32 again only to distinguish a missing
     * file from an access or filesystem error. */
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!count) { SDL_SetError("Invalid UTF-8 save path"); return FZERO_SAVE_PATH_ERROR; }
    wchar_t *wide = SDL_malloc((size_t)count * sizeof(*wide));
    if (!wide) { SDL_SetError("Out of memory"); return FZERO_SAVE_PATH_ERROR; }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count);
    DWORD attributes = GetFileAttributesW(wide);
    DWORD code = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    SDL_free(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES) return FZERO_SAVE_PATH_EXISTS;
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        return FZERO_SAVE_PATH_MISSING;
    SDL_SetError("Cannot inspect save path (Windows error %lu)", (unsigned long)code);
    return FZERO_SAVE_PATH_ERROR;
#else
    struct stat info;
    if (stat(path, &info) == 0) return FZERO_SAVE_PATH_EXISTS;
    if (errno == ENOENT) return FZERO_SAVE_PATH_MISSING;
    SDL_SetError("%s", strerror(errno));
    return FZERO_SAVE_PATH_ERROR;
#endif
}
