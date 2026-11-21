/* Linux fault injection for the save writer. The game runs in a test profile. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <libxml/tree.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fault_is(const char *name)
{
    const char *fault = getenv("LOTR_SAVE_FAULT");
    return fault && strcmp(fault, name) == 0;
}

static int is_save_temp(const char *path)
{
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    return strncmp(filename, "savegame.", 9) == 0 && strstr(filename, ".tmp.");
}

static void interrupt_writer(const char *stage)
{
    fprintf(stderr, "save-fault reached %s\n", stage);
    fflush(stderr);
    kill(getpid(), SIGKILL);
    _exit(125);
}

int xmlSaveFormatFileEnc(const char *path, xmlDocPtr doc, const char *encoding, int format)
{
    int (*save_xml)(const char *, xmlDocPtr, const char *, int) =
        dlsym(RTLD_NEXT, "xmlSaveFormatFileEnc");
    if (is_save_temp(path) && (fault_is("partial-kill") || fault_is("partial-error"))) {
        FILE *partial = fopen(path, "wb");
        if (!partial || fputs("<lotr_savegame", partial) == EOF || fclose(partial) != 0)
            _exit(125);
        if (fault_is("partial-kill"))
            interrupt_writer("partial-write");
        fprintf(stderr, "save-fault reached partial-error\n");
        errno = ENOSPC;
        return -1;
    }
    return save_xml(path, doc, encoding, format);
}

int rename(const char *source, const char *destination)
{
    int (*rename_file)(const char *, const char *) = dlsym(RTLD_NEXT, "rename");
    if (is_save_temp(source) && fault_is("before-rename"))
        interrupt_writer("before-rename");
    if (is_save_temp(source) && fault_is("rename-error")) {
        fprintf(stderr, "save-fault reached rename-error\n");
        errno = EIO;
        return -1;
    }
    return rename_file(source, destination);
}

int link(const char *source, const char *destination)
{
    int (*link_file)(const char *, const char *) = dlsym(RTLD_NEXT, "link");
    if (is_save_temp(source) && fault_is("link-error")) {
        fprintf(stderr, "save-fault reached link-error\n");
        errno = EIO;
        return -1;
    }
    if (is_save_temp(source) && fault_is("import-race")) {
        const char *competing_save = getenv("LOTR_COMPETING_SAVE");
        if (!competing_save || link_file(competing_save, destination) != 0)
            _exit(125);
        fprintf(stderr, "save-fault reached import-race\n");
    }
    return link_file(source, destination);
}
