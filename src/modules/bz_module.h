/* BreezeCAD module interface (C ABI).
 *
 * A module is a shared library (.dll / .so / .dylib) next to a module.json
 * manifest. BreezeCAD loads it, calls bz_module_entry() to get the module's
 * table of functions, checks abi_version and calls init() with the host's
 * table. Only C types cross the boundary, so a module can be written in C,
 * C++ (any compiler), Rust... and does not break when BreezeCAD is rebuilt.
 *
 * Rules:
 *  - Strings are UTF-8 and owned by whoever returns them. A string the host
 *    returns stays valid until the next call into the same host function or
 *    until the module returns from the call it is running in, whichever is first.
 *  - Functions return 0 (BZ_OK) on success, a negative BZ_E* code otherwise.
 *  - The module never throws across the boundary and never keeps host
 *    pointers after shutdown().
 *  - Both tables start with abi_version and struct_size: a newer host may
 *    append functions, a module checks struct_size before using them.
 *  - The read API is read-only: a module cannot change a document through it.
 *
 * A crash inside a module is noted: the next start of BreezeCAD turns that
 * module off and tells the user why.
 */
#ifndef BZ_MODULE_H
#define BZ_MODULE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BZ_MODULE_ABI_VERSION 1u

#if defined(_WIN32)
#define BZ_MODULE_EXPORT __declspec(dllexport)
#else
#define BZ_MODULE_EXPORT __attribute__((visibility("default")))
#endif

enum {
    BZ_OK         = 0,
    BZ_EFAIL      = -1, /* the module could not do what was asked */
    BZ_ENOTFOUND  = -2, /* no such object (index out of range, unknown id) */
    BZ_ENODATA    = -3, /* nothing to read (no assembly open, no geometry) */
    BZ_EARGUMENT  = -4  /* a null pointer or a bad value was passed */
};

enum {
    BZ_LOG_DEBUG   = 0,
    BZ_LOG_INFO    = 1,
    BZ_LOG_WARNING = 2,
    BZ_LOG_ERROR   = 3
};

/* One component of the open assembly, as the user sees it in the tree:
 * properties already take the part's values where the component leaves
 * them empty. Placement maps part coordinates to assembly coordinates (mm). */
typedef struct BzComponentInfo {
    uint64_t    id;
    const char* tree_name;    /* "Кронштейн:1" */
    const char* path;         /* part file, relative to the assembly, '/' separators */
    const char* designation;
    const char* name;
    const char* material;
    int32_t     quantity;
    int32_t     grounded;     /* 0 / 1 */
    int32_t     visible;      /* 0 / 1 */
    int32_t     has_geometry; /* 0 when the part file is missing or broken */
    double      origin[3];
    double      x_axis[3];
    double      z_axis[3];
} BzComponentInfo;

/* What BreezeCAD offers a module. */
typedef struct BzHostApi {
    uint32_t abi_version; /* BZ_MODULE_ABI_VERSION of the host */
    uint32_t struct_size; /* sizeof(BzHostApi) of the host */

    /* "0.2.0" */
    const char* (*host_version)(void);
    /* Writes to BreezeCAD's log (never leaves the computer). */
    void (*log)(int32_t level, const char* message);

    /* Read API for the active assembly; BZ_ENODATA when none is open. */
    int32_t (*assembly_name)(const char** name, const char** designation);
    int32_t (*component_count)(int32_t* count);
    int32_t (*component_at)(int32_t index, BzComponentInfo* out);
    /* Box of the component in assembly coordinates, mm. */
    int32_t (*component_bounds)(uint64_t id, double min_xyz[3], double max_xyz[3]);
} BzHostApi;

/* What a module offers BreezeCAD. */
typedef struct BzModuleApi {
    uint32_t abi_version; /* BZ_MODULE_ABI_VERSION the module was built with */
    uint32_t struct_size; /* sizeof(BzModuleApi) */

    /* Called once after loading. Non-zero: the module is not used and is
     * unloaded (the user sees it as failed). The host table stays valid
     * until shutdown. */
    int32_t (*init)(const BzHostApi* host);
    /* Called once before unloading (switching the module off, closing the program). */
    void (*shutdown)(void);
} BzModuleApi;

/* The one symbol every module exports. */
typedef const BzModuleApi* (*BzModuleEntryFn)(void);
#define BZ_MODULE_ENTRY_NAME "bz_module_entry"

#ifdef __cplusplus
}
#endif

#endif /* BZ_MODULE_H */
