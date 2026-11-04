; Inno Setup 6 script. Build with:  iscc /DAppVersion=1.0.0 /DDeployDir=<folder produced by windeployqt> packaging\installer.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef DeployDir
  #define DeployDir "..\dist\app"
#endif

[Setup]
AppId={{B7C1E5A4-3C2D-4E5F-9A61-5F0A7D2C9E10}
AppName=PLT 排版转换器
AppVersion={#AppVersion}
AppPublisher=好样纸样
DefaultDirName={autopf}\PLT排版转换器
DefaultGroupName=PLT 排版转换器
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=PLT-Converter-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
UninstallDisplayIcon={app}\plt_gui.exe

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："; Flags: unchecked

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion createallsubdirs

[Icons]
Name: "{autoprograms}\PLT 排版转换器"; Filename: "{app}\plt_gui.exe"
Name: "{autodesktop}\PLT 排版转换器"; Filename: "{app}\plt_gui.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\plt_gui.exe"; Description: "运行 PLT 排版转换器"; Flags: nowait postinstall skipifsilent
