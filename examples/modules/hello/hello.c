/* Example BreezeCAD module in plain C.
 *
 * Shows the whole contract: export bz_module_entry(), check the host's ABI
 * in init(), use the read API, clean up in shutdown(). On start it writes to
 * the log how many components the open assembly has. It also asks for one
 * main_tick (newer hosts) and there runs a command, as a module with its own
 * thread would: the number of open documents goes to the log.
 *
 * Build (the project builds it as bz_example_module):
 *   cc -shared -fPIC -I<breezecad>/src/modules hello.c -o libhello.so
 * and put libhello.so (hello.dll on Windows) next to module.json in a
 * folder under the modules folder.
 *
 * BZ_HELLO_FAIL: a variant whose init() refuses, for the unit tests.
 */
#include "bz_module.h"

#include <stddef.h>
#include <stdio.h>

static const BzHostApi* host = NULL;

static int32_t hello_init(const BzHostApi* h) {
    if (!h || h->abi_version != BZ_MODULE_ABI_VERSION || h->struct_size < offsetof(BzHostApi, request_main)) return BZ_EFAIL;   /* runs on 0.2 hosts too */
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
    if (BZ_HAS(host, request_main)) host->request_main();   /* any thread may ask */
    return BZ_OK;
#endif
}

static void hello_main_tick(void) {
    const char* reply = NULL;
    if (!host || !BZ_HAS(host, execute)) return;
    if (host->execute("{\"command\": \"app.documents\"}", &reply) == BZ_OK && reply) {
        char line[400];   /* the reply cut to fit: a log line, not the data */
        snprintf(line, sizeof line, "Пример модуля: открытые документы — %.300s", reply);
        host->log(BZ_LOG_INFO, line);
    }
}

static void hello_shutdown(void) {
    if (host) host->log(BZ_LOG_INFO, "Пример модуля: выключен");
    host = NULL;
}

static const BzModuleApi api = {BZ_MODULE_ABI_VERSION, sizeof(BzModuleApi), hello_init, hello_shutdown, hello_main_tick};

BZ_MODULE_EXPORT const BzModuleApi* bz_module_entry(void) { return &api; }
