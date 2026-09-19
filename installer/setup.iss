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

; The "add to PATH" task edits the environment, and this is what makes Setup
; broadcast WM_SETTINGCHANGE afterwards - without it a newly opened console
; would not see cvcli until the next logon.
ChangesEnvironment=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
russian.AddToPath=Добавить папку программы в PATH (чтобы вызывать cvcli из консоли)
english.AddToPath=Add the program folder to PATH (so cvcli can be run from a console)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
; Only the console renderer needs this, but PATH holds directories, not files,
; so what goes in is {app} - the same directory CVBuilder.exe sits in.
Name: "addtopath"; Description: "{cm:AddToPath}"

[Files]
Source: "{#BinDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\cvcli.exe"; DestDir: "{app}"; Flags: ignoreversion
; Read at startup as a starting point; the app never writes back to it, and
; "Save" on an untitled CV always opens a file dialog, so living under
; Program Files is fine.
Source: "..\sample_cv.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
; The bundled font, and the licence it ships under. Not optional: without it the
; program falls back to a system face, and the PDF stops being the same file it
; would be on another machine.
Source: "..\assets\fonts\*"; DestDir: "{app}\assets\fonts"; Flags: ignoreversion

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

[Code]
// PATH is edited here rather than with a [Registry] entry appending olddata:
// that appends a second copy on every repair or upgrade, and the uninstaller
// still has to take the directory back out without disturbing the rest of the
// variable. Which PATH is touched follows the install: a per-user install (the
// default, no administrator needed) edits HKCU, an elevated one the machine
// variable.

function EnvRootKey: Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE
  else Result := HKEY_CURRENT_USER;
end;

function EnvSubKey: String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else Result := 'Environment';
end;

function ReadPath: String;
begin
  { REG_EXPAND_SZ is returned unexpanded, so entries like %SystemRoot%\system32
    survive being read and written back. }
  if not RegQueryStringValue(EnvRootKey, EnvSubKey, 'Path', Result) then
    Result := '';
end;

procedure WritePath(const Value: String);
begin
  if not RegWriteExpandStringValue(EnvRootKey, EnvSubKey, 'Path', Value) then
    MsgBox('PATH could not be updated.', mbError, MB_OK);
end;

{ Compared without case and without a trailing backslash: the same directory
  spelled either way is still the same directory. }
function SamePathEntry(const A, B: String): Boolean;
begin
  Result := CompareText(RemoveBackslashUnlessRoot(Trim(A)),
                        RemoveBackslashUnlessRoot(Trim(B))) = 0;
end;

function PathHas(const Value, Dir: String): Boolean;
var
  Rest, Part: String;
  P: Integer;
begin
  Result := False;
  Rest := Value;
  repeat
    P := Pos(';', Rest);
    if P > 0 then begin
      Part := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest));
    end else begin
      Part := Rest;
      Rest := '';
    end;
    if SamePathEntry(Part, Dir) then Result := True;
  until Result or (Rest = '');
end;

procedure PathAdd(const Dir: String);
var
  Value: String;
begin
  Value := ReadPath;
  if PathHas(Value, Dir) then Exit;
  if (Value <> '') and (Value[Length(Value)] <> ';') then Value := Value + ';';
  WritePath(Value + Dir);
end;

procedure PathRemove(const Dir: String);
var
  Rest, Part, Kept: String;
  P: Integer;
  Changed: Boolean;
begin
  Rest := ReadPath;
  if Rest = '' then Exit;
  Kept := '';
  Changed := False;
  repeat
    P := Pos(';', Rest);
    if P > 0 then begin
      Part := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest));
    end else begin
      Part := Rest;
      Rest := '';
    end;
    if SamePathEntry(Part, Dir) then Changed := True
    else if Part <> '' then begin
      if Kept <> '' then Kept := Kept + ';';
      Kept := Kept + Part;
    end;
  until Rest = '';
  if Changed then WritePath(Kept);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    PathAdd(ExpandConstant('{app}'))
  { Unticking the task on an upgrade has to undo what a previous install did,
    otherwise the entry can only ever be removed by uninstalling. }
  else if CurStep = ssPostInstall then
    PathRemove(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    PathRemove(ExpandConstant('{app}'));
end;
