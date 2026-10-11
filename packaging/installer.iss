[Setup]
AppId={{17466D10-9257-45D3-A629-3653413F714E}
AppName=CellBook
AppVersion=1.0.0
AppPublisher=CellBook Project
DefaultDirName={localappdata}\Programs\CellBook
DefaultGroupName=CellBook
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=CellBook-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\CellBook.exe
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
[Files]
Source: "..\dist\CellBook\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\CellBook"; Filename: "{app}\CellBook.exe"
Name: "{autodesktop}\CellBook"; Filename: "{app}\CellBook.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\CellBook.exe"; Description: "Launch CellBook"; Flags: nowait postinstall skipifsilent
