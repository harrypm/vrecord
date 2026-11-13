; vrecord Windows installer (NSIS 3, Unicode). Built by
; Release/build-windows-installer.sh from the assembled portable package:
;   makensis -DVERSION=2026-03-03 -DVIVERSION=2026.3.3.0 -DPKGDIR=<package>
;            -DOUTFILE=<setup.exe> -DICON=<vrecord.ico> -DLICENSE=<LICENSE.txt>
;            windows-installer.nsi
;
; Per-user install (no administrator rights): %LOCALAPPDATA%\Programs\vrecord.
; That lets vrecord's updater run the next setup silently, without UAC:
;   setup.exe /S /D=<install folder>     (/D= last, unquoted)
; Only Blackmagic Desktop Video and a bash (MSYS2 or Git for Windows) live
; outside the package; the installer warns when either is missing.

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

!ifndef VERSION
  !error "VERSION not defined"
!endif
!ifndef PKGDIR
  !error "PKGDIR not defined"
!endif

!define APPNAME "vrecord"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\vrecord"
!define DECKLINK_CLSID "{BA6C6F44-6DA5-4DCE-94AA-EE2D1372A676}"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\vrecord"
InstallDirRegKey HKCU "Software\vrecord" "InstallDir"
BrandingText "vrecord ${VERSION}"

VIProductVersion "${VIVERSION}"
VIAddVersionKey "ProductName" "vrecord"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "vrecord installer"
VIAddVersionKey "LegalCopyright" "AMIA Open Source - see LICENSE.txt"
VIAddVersionKey "CompanyName" "AMIA Open Source"

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\VrecordQt6.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start vrecord"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE}"
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; the uninstaller removes $INSTDIR recursively: never let that be a shared
; folder such as "Program Files" - the install folder always ends in vrecord
Function EnsureOwnFolder
  ${GetFileName} "$INSTDIR" $0
  ${If} $0 != "vrecord"
    StrCpy $INSTDIR "$INSTDIR\vrecord"
  ${EndIf}
FunctionEnd

Function DirectoryLeave
  Call EnsureOwnFolder
FunctionEnd

; an open vrecord holds its exe: wait (silent update: the app is closing)
; or ask (interactive)
Function WaitForVrecordToClose
  StrCpy $1 0
  ${DoWhile} ${FileExists} "$INSTDIR\VrecordQt6.exe"
    ClearErrors
    FileOpen $0 "$INSTDIR\VrecordQt6.exe" a
    ${IfNot} ${Errors}
      FileClose $0
      ${Break}
    ${EndIf}
    ${If} ${Silent}
      IntOp $1 $1 + 1
      ${If} $1 > 120
        Abort "vrecord is still running"
      ${EndIf}
      Sleep 1000
    ${Else}
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "vrecord is running. Close it, then press Retry." IDRETRY retry_close
      Abort
      retry_close:
    ${EndIf}
  ${Loop}
FunctionEnd


Function CheckPrerequisites
  ${IfNot} ${Silent}
    SetRegView 64
    ReadRegStr $0 HKCR "CLSID\${DECKLINK_CLSID}\InprocServer32" ""
    SetRegView lastused
    ${If} $0 == ""
      MessageBox MB_OK|MB_ICONINFORMATION "Blackmagic Desktop Video was not found.$\r$\n$\r$\nvrecord needs it to capture from a DeckLink / Intensity card - install it from blackmagicdesign.com/support (vrecord itself is installed either way)."
    ${EndIf}
    ${IfNot} ${FileExists} "C:\msys64\usr\bin\bash.exe"
    ${AndIfNot} ${FileExists} "$PROGRAMFILES64\Git\bin\bash.exe"
      MessageBox MB_OK|MB_ICONINFORMATION "No bash was found (MSYS2 at C:\msys64 or Git for Windows).$\r$\n$\r$\nThe vrecord recording backend is a bash script - install MSYS2 (msys2.org) or Git for Windows before recording."
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "vrecord (required)" SecMain
  SectionIn RO
  Call EnsureOwnFolder
  Call WaitForVrecordToClose
  SetOutPath "$INSTDIR"
  ; a developer's package folder also holds run logs and local settings:
  ; never ship those
  File /r /x "VrecordQt6-*.log" /x "VrecordQt6-selftest.conf" /x "vrecord-qt6-update.ini" /x "portable.txt" "${PKGDIR}\*"
  ; an installed copy updates with the installer, not the portable zip
  Delete "$INSTDIR\portable.txt"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\vrecord"
  CreateShortcut "$SMPROGRAMS\vrecord\vrecord.lnk" "$INSTDIR\VrecordQt6.exe" "" "$INSTDIR\VrecordQt6.exe" 0
  CreateShortcut "$SMPROGRAMS\vrecord\vrecord (legacy GTK).lnk" "$INSTDIR\Vrecord.exe"
  CreateShortcut "$SMPROGRAMS\vrecord\Uninstall vrecord.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "Software\vrecord" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "vrecord"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "AMIA Open Source"
  WriteRegStr HKCU "${UNINSTKEY}" "URLInfoAbout" "https://github.com/amiaopensource/vrecord"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\VrecordQt6.exe"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" $0
  Call CheckPrerequisites
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\vrecord.lnk" "$INSTDIR\VrecordQt6.exe"
  ; the uninstaller removes only a desktop shortcut this installer made
  WriteRegDWORD HKCU "${UNINSTKEY}" "DesktopShortcut" 1
SectionEnd

Function .onInit
  ; a silent run is vrecord's updater: leave the desktop as the user has it
  ; (an existing shortcut keeps pointing at the same exe)
  ${If} ${Silent}
    SectionSetFlags ${SecDesktop} 0
  ${EndIf}
FunctionEnd

LangString DESC_SecMain ${LANG_ENGLISH} "vrecord with its Qt6 window, the legacy GTK window, ffmpeg-ma (DeckLink) and the bundled tools."
LangString DESC_SecDesktop ${LANG_ENGLISH} "A vrecord shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_SecMain)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  ; only remove a folder that really is a vrecord install
  ${GetFileName} "$INSTDIR" $0
  ${If} $0 == "vrecord"
  ${AndIf} ${FileExists} "$INSTDIR\VrecordQt6.exe"
    RMDir /r "$INSTDIR"
  ${Else}
    Delete "$INSTDIR\uninstall.exe"
  ${EndIf}
  ReadRegDWORD $1 HKCU "${UNINSTKEY}" "DesktopShortcut"
  ${If} $1 == 1
    Delete "$DESKTOP\vrecord.lnk"
  ${EndIf}
  RMDir /r "$SMPROGRAMS\vrecord"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegValue HKCU "Software\vrecord" "InstallDir"
  ; the installed copy's update settings go with it; other settings stay
  DeleteRegKey HKCU "Software\vrecord\vrecord-qt6\update"
  DeleteRegKey /ifempty HKCU "Software\vrecord\vrecord-qt6"
  DeleteRegKey /ifempty HKCU "Software\vrecord"
SectionEnd
