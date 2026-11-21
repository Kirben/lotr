/* Force-include only when compiling the instrumented Windows game object. */
#include <windows.h>
#include <libxml/tree.h>

int test_save_xml(const char *path, xmlDocPtr doc, const char *encoding, int format);
BOOL test_publish_save(LPCSTR source, LPCSTR destination, DWORD flags);

#define xmlSaveFormatFileEnc test_save_xml
#define MoveFileExA test_publish_save
