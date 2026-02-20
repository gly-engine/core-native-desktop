#ifndef GEDLL_H
#define GEDLL_H

#ifdef _WIN32
#include <windows.h>
#define LIB_HANDLE HMODULE
#define load_library(path) LoadLibrary(path)
#define get_symbol(lib, sym) GetProcAddress(lib, sym)
#define close_library(lib) FreeLibrary(lib)
#else
#include <dlfcn.h>
#define LIB_HANDLE void*
#define load_library(path) dlopen(path, RTLD_LAZY | RTLD_GLOBAL)
#define get_symbol(lib, sym) dlsym(lib, sym)
#define close_library(lib) dlclose(lib)
#endif

#endif
