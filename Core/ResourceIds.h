//==============================================================================
// ResourceIds.h - Shared resource identifiers
// Purpose:   Single definition point for resource IDs so that generated .rc
//            files (windres) and C++ code agree on the same symbols.
//            GeneratedIcon.rc includes this header instead of relying on
//            windres' implicit auto-assignment of undefined tokens.
//==============================================================================

#pragma once

// Application icon (see Build.py: generate_icon_resource()).
#define IDI_APP_ICON 101
