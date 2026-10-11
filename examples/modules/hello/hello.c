/* Example BreezeCAD module in plain C.
 *
 * Shows the whole contract: export bz_module_entry(), check the host's ABI
 * in init(), use the read API, clean up in shutdown(). On start it writes to
 * the log how many components the open assembly has.
 *
 * Build (the project builds it as bz_example_module):
 *   cc -shared -fPIC -I<breezecad>/src/modules hello.c -o libhello.so
 * and put libhello.so (hello.dll on Windows) next to module.json in a
 * folder under the modules folder.
 *
 * BZ_HELLO_FAIL: a variant whose init() refuses, for the unit tests.
 */
#include "bz_module.h"

#include <stdio.h>

static const BzHostApi* host = NULL;

static int32_t hello_init(const BzHostApi* h) {
    if (!h || h->abi_version != BZ_MODULE_ABI_VERSION || h->struct_size < sizeof(BzHostApi)) return BZ_EFAIL;
#ifdef BZ_HELLO_FAIL
    return BZ_EFAIL;
#else
    host = h;
    char    line[160];
    int32_t count = 0;
    if (host->component_count(&count) == BZ_OK)
        snprintf(line, sizeof line, "Пример модуля: в сборке компонентов — %d", (int)count);
    else
        snprintf(line, sizeof line, "Пример модуля запущен (BreezeCAD %s), сборка не открыта", host->host_version());
    host->log(BZ_LOG_INFO, line);
    return BZ_OK;
#endif
}

static void hello_shutdown(void) {
    if (host) host->log(BZ_LOG_INFO, "Пример модуля: выключен");
    host = NULL;
}

static const BzModuleApi api = {BZ_MODULE_ABI_VERSION, sizeof(BzModuleApi), hello_init, hello_shutdown};

BZ_MODULE_EXPORT const BzModuleApi* bz_module_entry(void) { return &api; }
