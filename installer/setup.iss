; Inno Setup script for CV Builder.
;
; Build the executables first (build.ps1), then compile this - or just run
; `.\build.ps1 -Installer`, which does both. The output lands in build\ as
; CVBuilder-<version>-setup.exe.
;
; One-off setup of the compiler:
;   winget install JRSoftware.InnoSetup
;
; The installer carries no runtime of its own: the two executables are
; statically linked, so installing is really just copying files, putting
; shortcuts where the user asked for them, and registering an uninstaller.

#define AppName "CV Builder"
; Where the built executables are. CMake puts them under the preset's own
; directory; build.ps1 passes the one it used, and the default is the preset a
; hand-run build would have produced.
#ifndef BinDir
  #define BinDir "..\build\windows-mingw\bin"
#endif
; Read straight out of the executable that was just built, whose version
; comes from res\version.h - so the version lives in exactly one file.
; This means the .exe has to exist before ISCC runs; build.ps1 -Installer
; builds first, and compiling this script on its own without a build will
; stop here with a clear error rather than shipping a stale number.
#define AppVersion GetStringFileInfo(BinDir + "\CVBuilder.exe", "ProductVersion")
#define AppPublisher "Daniil Mishin"
#define AppUrl "https://github.com/rochelvi/cv-builder"
#define AppExe "CVBuilder.exe"

[Setup]
; Never change AppId: it is how Windows recognises an existing installation and
; upgrades it in place instead of leaving two copies behind.
AppId={{8F3C6A21-4D5E-4B7A-9C10-2E6B5D4A9F31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
; The program is small and self-contained, so the directory page is the only
; question worth asking; the Start menu folder is not.
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\build
OutputBaseFilename=CVBuilder-{#AppVersion}-setup
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}

; Ask for no rights by default and let the user raise it on the first page:
; a per-user install into %LOCALAPPDATA% needs no administrator at all, which
; is the friendlier default for a tool like this.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; The executables are 64-bit.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; Offers to close a running copy rather than failing on a locked .exe.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BinDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\cvcli.exe"; DestDir: "{app}"; Flags: ignoreversion
; Read at startup as a starting point; the app never writes back to it, and
; "Save" on an untitled CV always opens a file dialog, so living under
; Program Files is fine.
Source: "..\sample_cv.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Where the app remembers the light/dark choice. Removed on uninstall so that
; nothing of ours is left in the registry.
Root: HKCU; Subkey: "Software\{#AppName}"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent
