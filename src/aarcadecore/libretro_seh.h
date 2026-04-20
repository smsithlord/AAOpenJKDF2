#ifndef LIBRETRO_SEH_H
#define LIBRETRO_SEH_H

/*
 * Portable structured exception handling for Libretro core calls.
 *
 * On MSVC: maps to __try / __except — catches access violations etc. raised
 * inside core code so the engine can survive a misbehaving core.
 * On other compilers: degenerates to plain braces (no protection). The DLL
 * is currently MSVC-only; the macros let the host source compile cleanly
 * elsewhere if/when a Linux/Mac aarcadecore is built.
 */

#if defined(_MSC_VER)
  #include <excpt.h>
  #define LIBRETRO_TRY                __try
  #define LIBRETRO_EXCEPT(action)     __except(EXCEPTION_EXECUTE_HANDLER) { action; }
  #define LIBRETRO_HAS_SEH            1
#else
  #define LIBRETRO_TRY                if (1)
  #define LIBRETRO_EXCEPT(action)     else { action; }
  #define LIBRETRO_HAS_SEH            0
#endif

#endif /* LIBRETRO_SEH_H */
