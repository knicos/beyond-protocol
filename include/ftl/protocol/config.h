/**
 * @file config.h
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 * 
 * To be CMake processed
 */

#pragma once

#ifdef WIN32
#ifdef BUILD_SHARED
#define FTLEXPORT __declspec(dllexport)
#else
#define FTLEXPORT __declspec(dllimport)
#endif
#else // WIN32
#define FTLEXPORT
#endif

/* #undef HAVE_URIPARSESINGLE */
/* #undef HAVE_LIBARCHIVE */
#define HAVE_GNUTLS
/* #undef HAVE_PYTHON */

/* #undef ENABLE_PROFILER */

extern const char *FTL_BRANCH;
extern const char *FTL_VERSION_LONG;
extern const char *FTL_VERSION;
extern const int FTL_VERSION_MAJOR;
extern const int FTL_VERSION_MINOR;
extern const int FTL_VERSION_PATCH;
extern const int FTL_VERSION_COMMITS;
extern const char *FTL_VERSION_SHA1;

#define FTL_SOURCE_DIRECTORY "/home/nick/repos/beyond-protocol"

#define FTL_LOCAL_CONFIG_ROOT "/home/nick/.config/ftl"
#define FTL_LOCAL_CACHE_ROOT "/home/nick/.cache/ftl"
#define FTL_LOCAL_DATA_ROOT "/home/nick/.local/share/ftl"

#define FTL_GLOBAL_CONFIG_ROOT "install/share/ftl"
#define FTL_GLOBAL_CACHE_ROOT "install/share/ftl"
#define FTL_GLOBAL_DATA_ROOT "install/share/ftl"
