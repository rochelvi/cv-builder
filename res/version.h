// Version and authorship - the single place where they are written down.
//
// res/app.rc and res/cli.rc include this, so both executables always claim the
// same version. installer/setup.iss then reads that version back out of the
// built CVBuilder.exe, so the installer name, the entry in "Apps & features"
// and the file properties cannot drift apart either.
//
// To release a new version, change the three numbers below and nothing else.
#pragma once

// ------------------------------------------------------------- the version
// The patch number always shows, including when it is 0. A security fix that
// ships as 1.2.1 is then something an installed copy can be asked about - the
// file properties and the entry in "Apps & features" answer "am I patched?"
// on a machine with no network and no access to the repository, which a tag
// on GitHub cannot do. Bump it for any release that changes a binary.
#define VER_MAJOR 1
#define VER_MINOR 2
#define VER_PATCH 0

// Windows compares a four-part number when it decides which of two files is
// newer, and shows a string to people. Both are built from the numbers above
// so they cannot fall out of step; the two-step stringification is the usual
// dance to expand the macros before turning them into text. The fourth part
// is left at 0 - there is no build counter to put there.
#define VER_STR_(x) #x
#define VER_STR(x) VER_STR_(x)
#define VER_FILEVERSION VER_MAJOR, VER_MINOR, VER_PATCH, 0
#define VER_VERSION_STR VER_STR(VER_MAJOR) "." VER_STR(VER_MINOR) "." VER_STR(VER_PATCH)

// ------------------------------------------------------------ who wrote it
#define VER_PRODUCT          "CV Builder"
#define VER_COMPANY          "Daniil Mishin"
#define VER_COPYRIGHT        "© 2026 Daniil Mishin. Лицензия GPL-3.0"
