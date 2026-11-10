; Lodestone Windows installer (Inno Setup 6).
; Built by CI:  iscc /DAppVersion=1.0.0 /DBuildDir=..\build installer\Lodestone.iss
; Installs the VST3 and CLAP plug-ins to the standard shared folders (every DAW
; scans them) and, optionally, the standalone app with a Start menu shortcut.
; Matches the rest of the LowKei Audio family.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build"
#endif
; JUCE puts the plug-in targets under build\src because the plugin is added
; with add_subdirectory(src).
#define Artefacts BuildDir + "\src\Lodestone_artefacts\Release"

[Setup]
; AppId identifies this product to Windows for upgrades and uninstall.
; Never change it once shipped, and never reuse it for another plugin.
AppId={{7307B7CF-0CC1-44F6-BC7C-DAAED2557CBE}
AppName=Lodestone
AppVersion={#AppVersion}
AppVerName=Lodestone {#AppVersion}
AppPublisher=LowKei Audio
DefaultDirName={autopf}\LowKei\Lodestone
DefaultGroupName=Lodestone
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Admin rights are needed to write to C:\Program Files\Common Files\VST3.
PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename=Lodestone-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Lodestone {#AppVersion}
UninstallDisplayIcon={app}\Lodestone.exe

[Types]
Name: "full"; Description: "VST3 and CLAP plug-ins and standalone app"
Name: "plugins"; Description: "Plug-ins only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Serato Studio, FL Studio, Ableton Live)"; Types: full plugins custom
Name: "clap"; Description: "CLAP plug-in (FL Studio, Bitwig Studio, Reaper)"; Types: full plugins custom
Name: "standalone"; Description: "Standalone app (no DAW needed)"; Types: full

[Files]
; The VST3 is a bundle folder; copy the whole folder, not just the inner DLL.
Source: "{#Artefacts}\VST3\Lodestone.vst3\*"; DestDir: "{commoncf64}\VST3\Lodestone.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; CLAP is a single file in the shared CLAP folder.
Source: "{#Artefacts}\CLAP\Lodestone.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\Lodestone.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\docs\README.pdf"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\Lodestone"; Filename: "{app}\Lodestone.exe"; Components: standalone
Name: "{group}\Lodestone README"; Filename: "{app}\README.pdf"
Name: "{group}\Uninstall Lodestone"; Filename: "{uninstallexe}"

[UninstallDelete]
; Remove the plug-in bundle folder itself, which Inno only empties by default.
Type: dirifempty; Name: "{commoncf64}\VST3\Lodestone.vst3"
