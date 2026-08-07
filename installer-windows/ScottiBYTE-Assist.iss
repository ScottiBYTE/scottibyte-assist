#define MyAppName "ScottiBYTE Assist"
#define MyAppVersion "0.2.4"
#define MyAppPublisher "ScottiBYTE"
#define MyAppExeName "scottibyte-assist.exe"

[Setup]
AppId={{B54E1178-2B50-4C55-90B1-5D291F76F665}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\ScottiBYTE Assist
DefaultGroupName=ScottiBYTE Assist
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=output
OutputBaseFilename=ScottiBYTE-Assist-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "C:\Users\scott\scottibyte-assist\deploy-windows\*"; DestDir: "{app}"; Excludes: "ScottiBYTE-Assist.cmd"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\ScottiBYTE Assist"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\ScottiBYTE Assist"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch ScottiBYTE Assist"; Flags: nowait postinstall skipifsilent
