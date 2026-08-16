#define MyAppName "ScottiBYTE Assist"
#define MyAppVersion "0.4.6"
#define MyAppPublisher "ScottiBYTE"
#define MyAppExeName "scottibyte-assist.exe"

[Setup]
AppId={{B54E1178-2B50-4C55-90B1-5D291F76F665}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}

DefaultDirName={autopf}\ScottiBYTE Assist
UsePreviousAppDir=no
DisableDirPage=yes

DefaultGroupName=ScottiBYTE Assist
DisableProgramGroupPage=yes

PrivilegesRequired=admin

SetupIconFile=..\assets\scottibyte-assist.ico
WizardSmallImageFile=C:\Users\scott\scottibyte-assist\assets\scottibyte-assist.png
UninstallDisplayIcon={app}\{#MyAppExeName}

OutputDir=output
OutputBaseFilename=ScottiBYTE-Assist-Setup-{#MyAppVersion}

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

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
Filename: "{sys}\sc.exe"; Parameters: "create ScottiBYTEAssistService binPath= ""{app}\scottibyte-assist-service.exe"" start= auto DisplayName= ""ScottiBYTE Assist Privileged Service"""; Flags: runhidden waituntilterminated
Filename: "{sys}\sc.exe"; Parameters: "description ScottiBYTEAssistService ""Provides privileged Windows support functions for ScottiBYTE Assist."""; Flags: runhidden waituntilterminated
Filename: "{sys}\sc.exe"; Parameters: "start ScottiBYTEAssistService"; Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "Launch ScottiBYTE Assist"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\sc.exe"; Parameters: "stop ScottiBYTEAssistService"; Flags: runhidden waituntilterminated; RunOnceId: "StopScottiBYTEAssistService"
Filename: "{sys}\sc.exe"; Parameters: "delete ScottiBYTEAssistService"; Flags: runhidden waituntilterminated; RunOnceId: "DeleteScottiBYTEAssistService"
