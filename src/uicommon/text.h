// The words an editor puts on the model's own concepts.
//
// These are not model data - the file format stores an id, not a caption - and
// they are not the property of one front end either, since both the Win32 and
// the portable UI label the same eight colour roles and the same eight
// sections. They live here, in UTF-8, so the two cannot drift apart and neither
// dictates a string type to the other.
#pragma once

#include <string>

#include "model.h"

namespace cvb {
namespace uicommon {

// Parallel to ThemeRole and to kSectionIds.
extern const char* const kThemeRoles[TR_Count];
extern const char* const kSectionNames[kSectionCount];

// The editor's name for a section id; empty for an id this build does not know.
const char* sectionName(const std::string& id);

}  // namespace uicommon
}  // namespace cvb
