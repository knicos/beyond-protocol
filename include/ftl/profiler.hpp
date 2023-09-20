/**
 * @file profiler.hpp
 * @copyright Copyright (c) 2020-2022 University of Turku, MIT License
 * @author Nicolas Pope, Sebastian Hahta
 */

#pragma once

#include <ftl/protocol/config.h>
#include <string>

inline void FTL_PROFILE_LOG(const std::string& message) {
#ifdef TRACY_ENABLE
	TracyMessage(message.c_str(), message.size());
#endif
}

namespace detail
{
    /** Get a persistent pointer to a given string (creates a new stable const
     *  char array if such string doesn't exist or returns a pointer to previously
     *  created const char array. The pointer should be cached)*/
    const char* GetPersistentString(const char* String);
    inline const char* GetPersistentString(const std::string& String) { return GetPersistentString(String.c_str()); }
}

#define PROFILER_RUNTIME_PERSISTENT_NAME(name) detail::GetPersistentString(name)

#ifdef TRACY_ENABLE

#include <tracy/Tracy.hpp>

#define FTL_PROFILE_SCOPE(LABEL) ZoneScopedN(LABEL)

// NOTE: Tracy expects Label to be a pointer to same address (this should be the case
//       with GCC and MSVC with string pooling). If not, label has to be defined
//       separately before use (static const char* ...) and exported if necessary

/** Mark (secondary) frame start and stop. Each FTL_PROFILE_FRAME_BEGIN MUST be matched
 * with FTL_PROFILE_FRAME_END */
#define FTL_PROFILE_FRAME_BEGIN(LABEL) FrameMarkStart(#LABEL)
#define FTL_PROFILE_FRAME_END(LABEL) FrameMarkEnd(#LABEL)

/** Mark end of primary frame (main rendering/capture loop etc, if applicable) */
#define FTL_PROFILE_PRIMARY_FRAME_END() FrameMark

/// deprecated
#define FTL_Profile(LABEL, LIMIT) FTL_PROFILE_SCOPE(LABEL)

#if defined(TRACY_FIBERS)

#include <tracy/TracyC.h>

namespace detail
{

struct TracyFiberScope
{
    TracyFiberScope(const char* Name) { TracyFiberEnter(Name); }
    ~TracyFiberScope() { TracyFiberLeave; }
    TracyFiberScope(const TracyFiberScope&) = delete;
    TracyFiberScope& operator=(const TracyFiberScope&) = delete;
};

}

/** Tracy fiber profiling is used to track async calls. Normal tracy zones can
 *  not be used as they must end in the same thread they were started in.
 *  See: Tracy manual, section 3.13.3.1 (Feb 23 2023).
 */
#define PROFILER_ASYNC_ZONE_CTX(varname) TracyCZoneCtx varname
#define PROFILER_ASYNC_ZONE_CTX_ASSIGN(varname_target, varname_source) varname_target = varname_source;
/** Uses Tracy fibers (if available), name will be used as fiber name in profiler */
#define PROFILER_ASYNC_ZONE_SCOPE(name) detail::TracyFiberScope TracyScope_Fiber(name)
#define PROFILER_ASYNC_ZONE_BEGIN(name, ctx) TracyCZoneN(ctx, name, 1)
#define PROFILER_ASYNC_ZONE_BEGIN_ANON(ctx) TracyCZone(ctx, 1)
#define PROFILER_ASYNC_ZONE_END(ctx) TracyCZoneEnd(ctx)

#else // TRACY_FIBERS

#define PROFILER_ASYNC_ZONE_CTX(varname)
#define PROFILER_ASYNC_ZONE_CTX_ASSIGN(varname_in, varname_out)
#define PROFILER_ASYNC_ZONE_SCOPE(name)
#define PROFILER_ASYNC_ZONE_BEGIN(name, ctx)
#define PROFILER_ASYNC_ZONE_BEGIN_ANON(ctx)
#define PROFILER_ASYNC_ZONE_END(ctx)

#endif // TRACY_FIBERS

#else // TRACY_ENABLE

#define PROFILER_ASYNC_ZONE_CTX(LABEL)
#define PROFILER_ASYNC_ZONE_SCOPE(LABEL) {}

#define FTL_PROFILE_FRAME_BEGIN(LABEL) {}
#define FTL_PROFILE_FRAME_END(LABEL) {}
#define FTL_PROFILE_PRIMARY_FRAME_END() {}
#define FTL_PROFILE_SCOPE(LABEL) {}

/// deprectated
#define FTL_Profile(LABEL, LIMIT) {}

#endif // TRACY_ENABLE
