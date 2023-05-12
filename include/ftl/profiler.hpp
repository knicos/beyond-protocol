/**
 * @file profiler.hpp
 * @copyright Copyright (c) 2020-2022 University of Turku, MIT License
 * @author Nicolas Pope, Sebastian Hahta
 */

#pragma once

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

#else

#define FTL_PROFILE_FRAME_BEGIN(LABEL) {}
#define FTL_PROFILE_FRAME_END(LABEL) {}
#define FTL_PROFILE_PRIMARY_FRAME_END() {}
#define FTL_PROFILE_SCOPE(LABEL) {}

/// deprectated
#define FTL_Profile(LABEL, LIMIT) {}

#endif
