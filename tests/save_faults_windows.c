/* The parent test process terminates this worker at a controlled boundary. */
#include <windows.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fault_is(const char *stage)
{
    const char *fault = getenv("LOTR_SAVE_FAULT");
    return fault && strcmp(fault, stage) == 0;
}

static void wait_for_termination(const char *stage)
{
    fprintf(stderr, "save-fault reached %s\n", stage);
    fflush(stderr);
    Sleep(INFINITE);
}

int test_save_xml(const char *path, xmlDocPtr doc, const char *encoding, int format)
{
    if (fault_is("partial-write")) {
        FILE *file = fopen(path, "wb");
        if (!file)
            return -1;
        int written = fputs("<lotr_savegame", file);
        if (fclose(file) != 0 || written == EOF)
            return -1;
        wait_for_termination("partial-write");
        return -1;
    }
    return xmlSaveFormatFileEnc(path, doc, encoding, format);
}

BOOL test_publish_save(LPCSTR source, LPCSTR destination, DWORD flags)
{
    if (fault_is("before-publish")) {
        wait_for_termination("before-publish");
        return FALSE;
    }
    return MoveFileExA(source, destination, flags);
}
