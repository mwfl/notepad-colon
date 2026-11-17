; Notepad:: installer. Built by scripts/build-installer.ps1, which stages the
; product files and passes these defines:
;   VERSION      1.3.2          VERSION4   1.3.2.0
;   ARCH         x64 | arm64    STAGE      directory holding the product files
;   FILELIST     generated include defining the InstallFiles/UninstallFiles macros
;   OUTFILE      path of the installer to write
;
; The installer is per-user: it needs no administrator rights, installs under
; %LOCALAPPDATA%\Programs and registers itself for the current user only.

Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "x64.nsh"
!include "WordFunc.nsh"
!include "${FILELIST}"

!define PRODUCT "Notepad::"
!define FOLDER "Notepad Colon"
!define PUBLISHER "AtomSprout"
!define EXE "notepad-colon.exe"
!define UNINSTALLER "uninstall.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\NotepadColon"
!define WEBSITE "https://github.com/mwfl/notepad-colon"

Name "${PRODUCT}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${FOLDER}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
BrandingText "${PRODUCT} ${VERSION}"

VIProductVersion "${VERSION4}"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 mwfl contributors"

!define MUI_ICON "..\..\assets\notepad-colon.ico"
!define MUI_UNICON "..\..\assets\notepad-colon.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\nsis3-metro.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\nsis3-metro.bmp"
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; The first language is the fallback; the rest are picked from the Windows
; display language and match the editor's own interface languages.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "Hindi"

LangString TextProgram ${LANG_ENGLISH} "${PRODUCT} (required)"
LangString TextProgram ${LANG_SIMPCHINESE} "${PRODUCT}(必需)"
LangString TextProgram ${LANG_TRADCHINESE} "${PRODUCT}(必要)"
LangString TextProgram ${LANG_SPANISH} "${PRODUCT} (obligatorio)"
LangString TextProgram ${LANG_FRENCH} "${PRODUCT} (obligatoire)"
LangString TextProgram ${LANG_HINDI} "${PRODUCT} (आवश्यक)"

LangString TextDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString TextDesktop ${LANG_SIMPCHINESE} "桌面快捷方式"
LangString TextDesktop ${LANG_TRADCHINESE} "桌面捷徑"
LangString TextDesktop ${LANG_SPANISH} "Acceso directo en el escritorio"
LangString TextDesktop ${LANG_FRENCH} "Raccourci sur le bureau"
LangString TextDesktop ${LANG_HINDI} "डेस्कटॉप शॉर्टकट"

LangString TextRunning ${LANG_ENGLISH} "${PRODUCT} is running. Close it, then choose Retry."
LangString TextRunning ${LANG_SIMPCHINESE} "${PRODUCT} 正在运行。请先关闭它,然后选择“重试”。"
LangString TextRunning ${LANG_TRADCHINESE} "${PRODUCT} 正在執行。請先關閉它,然後選擇「重試」。"
LangString TextRunning ${LANG_SPANISH} "${PRODUCT} se está ejecutando. Ciérrelo y elija Reintentar."
LangString TextRunning ${LANG_FRENCH} "${PRODUCT} est en cours d'exécution. Fermez-le, puis choisissez Réessayer."
LangString TextRunning ${LANG_HINDI} "${PRODUCT} चल रहा है। इसे बंद करें, फिर पुनः प्रयास चुनें।"

LangString TextNeeds64 ${LANG_ENGLISH} "${PRODUCT} needs 64-bit Windows 10 or later."
LangString TextNeeds64 ${LANG_SIMPCHINESE} "${PRODUCT} 需要 64 位 Windows 10 或更高版本。"
LangString TextNeeds64 ${LANG_TRADCHINESE} "${PRODUCT} 需要 64 位元 Windows 10 或更新版本。"
LangString TextNeeds64 ${LANG_SPANISH} "${PRODUCT} requiere Windows 10 de 64 bits o posterior."
LangString TextNeeds64 ${LANG_FRENCH} "${PRODUCT} nécessite Windows 10 64 bits ou une version ultérieure."
LangString TextNeeds64 ${LANG_HINDI} "${PRODUCT} के लिए 64-बिट Windows 10 या बाद का संस्करण चाहिए।"

; Waits until the installed editor is closed: its files cannot be replaced or
; removed while it runs. A silent run fails instead of waiting for nobody.
!macro WaitForEditor
    !define /redef _label "editor_${__LINE__}"
    ${_label}_check:
    IfFileExists "$INSTDIR\${EXE}" 0 ${_label}_done
    ClearErrors
    FileOpen $0 "$INSTDIR\${EXE}" a
    IfErrors 0 ${_label}_free
    IfSilent 0 +3
        SetErrorLevel 2
        Abort
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(TextRunning)" IDRETRY ${_label}_check
    Abort
    ${_label}_free:
    FileClose $0
    ${_label}_done:
!macroend

Function .onInit
    ${IfNot} ${RunningX64}
    ${AndIfNot} ${IsNativeARM64}
        MessageBox MB_OK|MB_ICONSTOP "$(TextNeeds64)" /SD IDOK
        Abort
    ${EndIf}
!if "${ARCH}" == "arm64"
    ${IfNot} ${IsNativeARM64}
        MessageBox MB_OK|MB_ICONSTOP "$(TextNeeds64)" /SD IDOK
        Abort
    ${EndIf}
!endif
FunctionEnd

Section "$(TextProgram)" SectionProgram
    SectionIn RO
    !insertmacro WaitForEditor
    !insertmacro InstallFiles
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\${UNINSTALLER}"
    CreateShortcut "$SMPROGRAMS\${FOLDER}.lnk" "$INSTDIR\${EXE}"

    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${EXE},0"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\${UNINSTALLER}"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\${UNINSTALLER}" /S'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "${WEBSITE}"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${INSTALLED_KB}
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section /o "$(TextDesktop)" SectionDesktop
    CreateShortcut "$DESKTOP\${FOLDER}.lnk" "$INSTDIR\${EXE}"
SectionEnd

; Removes one Explorer verb the editor added (Tools > Add Explorer Context
; Menu), but only when it carries the editor's owner marker and still points
; into this installation, so another copy's or another program's entry stays.
!macro un.RemoveVerb CLASS
    ReadRegStr $0 HKCU "Software\Classes\${CLASS}\shell\NotepadColon" "NotepadColonOwner"
    ReadRegStr $1 HKCU "Software\Classes\${CLASS}\shell\NotepadColon" "Icon"
    ${If} $0 == "mwfl.notepad-colon"
        ${WordFind} "$1" "$INSTDIR\" "E+1{" $2
        ${IfNot} ${Errors}
            DeleteRegKey HKCU "Software\Classes\${CLASS}\shell\NotepadColon"
        ${EndIf}
    ${EndIf}
!macroend

Section "Uninstall"
    !insertmacro WaitForEditor
    !insertmacro UninstallFiles
    Delete "$INSTDIR\${UNINSTALLER}"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\${FOLDER}.lnk"
    Delete "$DESKTOP\${FOLDER}.lnk"
    !insertmacro un.RemoveVerb "*"
    !insertmacro un.RemoveVerb "Directory"
    !insertmacro un.RemoveVerb "Directory\Background"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    ; Preferences, sessions and recovery data are the user's and are kept.
SectionEnd
