/* MinGW-w64 as MSYS2 ships it since October 2026 (GCC 16.2): fesetenv() in
 * libmingwex refers to __mingw_fe_dfl_env (the default floating-point
 * environment, FE_DFL_ENV), and only the startup code of programs defines
 * it. A DLL linked with -static then fails: "undefined reference to
 * `__mingw_fe_dfl_env'". The definition here is the CRT's own ({0, 0}: reset
 * to the state at start-up); weak, so a CRT that defines it again wins. */
#include <fenv.h>

__attribute__((weak)) const fenv_t __mingw_fe_dfl_env = {0, 0};
