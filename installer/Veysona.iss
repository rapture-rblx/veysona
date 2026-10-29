; Veysona Studio branded installation package.
; Compiled with Inno Setup 6 on a Windows x64 build runner, after CMake tests pass.
; Never point the [Files] section at an unverified build output.
#define AppVersion "1.31.0"
#define StageDir "..\stage\Veysona"
#define BrandDir "..\assets\branding"

[Setup]
AppId={{E16D8A0C-721F-4F77-BE94-E9466253D87A}
AppName=Veysona Studio
AppVerName=Veysona Studio {#AppVersion}
AppVersion={#AppVersion}
AppPublisher=Veysona Community
AppCopyright=Copyright (c) Veysona contributors
DefaultDirName={autopf}\Veysona Studio
DefaultGroupName=Veysona Studio
OutputBaseFilename=Veysona-Studio-Setup-{#AppVersion}
OutputDir=..\dist
SetupIconFile={#BrandDir}\Veysona.ico
WizardImageFile={#BrandDir}\wizard-large.bmp
WizardSmallImageFile={#BrandDir}\wizard-small.bmp
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Veysona.exe
AllowNoIcons=yes
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "vst3"; Description: "Install the five bundled Veysona VST3 plugins (available to other DAWs)"; GroupDescription: "Optional components:"

[Files]
Source: "{#StageDir}\Veysona.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\veysona_audio_tool.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\tools\install_recorded_instruments.py"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#StageDir}\tools\veysona_update_client.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#StageDir}\Install_Recorded_Instruments.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\RECORDED_INSTRUMENTS.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
; These plugins are also available in the portable ZIP, and in the local build tree.
Source: "{#StageDir}\VST3\*"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3

[Icons]
Name: "{autoprograms}\Veysona Studio"; Filename: "{app}\Veysona.exe"; IconFilename: "{app}\Veysona.exe"
Name: "{autodesktop}\Veysona Studio"; Filename: "{app}\Veysona.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Veysona.exe"; Description: "Launch Veysona Studio"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Only application-created cache goes here; never delete user projects or global VST3 folders.
Type: filesandordirs; Name: "{app}\cache"
