#define MyAppName "VOFA2"
#define MyAppVersion "0.1.0"
#define MyAppExeName "vofa2_app.exe"

[Setup]
AppId={{8F3C2A4E-6B1D-4C7A-9E2F-1A5D7B3C9E60}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=VOFA2
; Per-user install: no UAC prompt, no administrator rights required.
PrivilegesRequired=lowest
DefaultDirName={autopf}\VOFA2
DefaultGroupName=VOFA2
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=VOFA2-{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"

[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\VOFA2"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\VOFA2"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 VOFA2"; Flags: nowait postinstall skipifsilent
