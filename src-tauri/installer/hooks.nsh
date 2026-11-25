; Installer hooks for the Tauri NSIS template: the "add to PATH" option the
; Inno Setup installer had, so cvcli runs by name from any console.
;
; The directory goes into PATH once, on a yes; a no on a later install takes it
; out again, and so does the uninstaller. Nothing else in PATH is touched. The
; per-user install edits HKCU\Environment, the per-machine one the system
; variable. Consoles already open do not see the change; a new one does.

!define CVB_MACHINE_ENV "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"

${StrRep}
${UnStrRep}
${UnStrLoc}

; Reads PATH for the current install mode into $R0. Sets $R1 to 1 when the value
; is too long to be read whole: NSIS cuts strings at NSIS_MAX_STRLEN, and
; writing a cut value back would destroy the user's PATH, so then nothing is
; written at all.
!macro CVB_READ_PATH
  ${If} $MultiUser.InstallMode == "AllUsers"
    ReadRegStr $R0 HKLM "${CVB_MACHINE_ENV}" "Path"
  ${Else}
    ReadRegStr $R0 HKCU "Environment" "Path"
  ${EndIf}
  StrLen $R2 $R0
  StrLen $R3 $INSTDIR
  IntOp $R2 $R2 + $R3
  IntOp $R2 $R2 + 4
  ${If} $R2 >= ${NSIS_MAX_STRLEN}
    StrCpy $R1 1
  ${Else}
    StrCpy $R1 0
  ${EndIf}
!macroend

!macro CVB_WRITE_PATH
  ${If} $MultiUser.InstallMode == "AllUsers"
    WriteRegExpandStr HKLM "${CVB_MACHINE_ENV}" "Path" $R0
  ${Else}
    WriteRegExpandStr HKCU "Environment" "Path" $R0
  ${EndIf}
  ; Tell running programs - Explorer above all - that the environment changed.
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
!macroend

; Removes every ";$INSTDIR;" entry from $R0 and trims the separators it leaves.
; UN is "" in the installer and "Un" in the uninstaller.
!macro CVB_STRIP_ENTRY UN
  StrCpy $R0 ";$R0;"
  ${${UN}StrRep} $R0 $R0 ";$INSTDIR;" ";"
  ${${UN}StrRep} $R0 $R0 ";$INSTDIR\;" ";"
  ${${UN}StrRep} $R0 $R0 ";;" ";"
  StrCpy $R2 $R0 1
  ${If} $R2 == ";"
    StrCpy $R0 $R0 "" 1
  ${EndIf}
  StrCpy $R2 $R0 1 -1
  ${If} $R2 == ";"
    StrCpy $R0 $R0 -1
  ${EndIf}
!macroend

!macro NSIS_HOOK_POSTINSTALL
  !insertmacro CVB_READ_PATH
  ${If} $R1 == 0
    MessageBox MB_YESNO|MB_ICONQUESTION "Добавить папку программы в PATH, чтобы cvcli запускался из любой консоли?" /SD IDYES IDNO cvb_path_no
      ; Yes: add it, unless an earlier install already did.
      ${StrLoc} $R3 ";$R0;" ";$INSTDIR;" ">"
      ${If} $R3 == ""
        ${If} $R0 == ""
          StrCpy $R0 "$INSTDIR"
        ${Else}
          StrCpy $R0 "$R0;$INSTDIR"
        ${EndIf}
        !insertmacro CVB_WRITE_PATH
      ${EndIf}
      Goto cvb_path_done
    cvb_path_no:
      ; No: take out an entry a previous install left.
      StrCpy $R4 $R0
      !insertmacro CVB_STRIP_ENTRY ""
      ${If} $R0 != $R4
        !insertmacro CVB_WRITE_PATH
      ${EndIf}
    cvb_path_done:
  ${Else}
    DetailPrint "PATH слишком длинный, чтобы его безопасно изменить; папка программы в него не добавлена."
  ${EndIf}
!macroend

!macro NSIS_HOOK_PREUNINSTALL
  !insertmacro CVB_READ_PATH
  ${If} $R1 == 0
    ${UnStrLoc} $R3 ";$R0;" ";$INSTDIR" ">"
    ${If} $R3 != ""
      !insertmacro CVB_STRIP_ENTRY "Un"
      !insertmacro CVB_WRITE_PATH
    ${EndIf}
  ${EndIf}
!macroend
