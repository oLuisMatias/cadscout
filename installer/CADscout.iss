#define PackageRoot AddBackslash(SourcePath) + "..\build\package"

; The version comes from build_all.bat (/DAppVersion=...), which reads it from vcpkg.json.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{A0C3CF48-9BE9-4D22-BD4F-8E8977EA62D5}
AppName=CADscout
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\CADscout
DefaultGroupName=CADscout
OutputDir=..\build
OutputBaseFilename=CADscoutSetup
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayIcon={app}\CADscoutLight.exe

[Types]
Name: "light"; Description: "Light Viewer only"
Name: "full"; Description: "Full edition"

[Components]
Name: "light"; Description: "Light Viewer"; Types: light full; Flags: fixed
Name: "full"; Description: "Full edition"; Types: full; Flags: fixed

[Files]
Source: "{#PackageRoot}\Light\*"; DestDir: "{app}"; Components: light; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageRoot}\Full\*"; DestDir: "{app}"; Components: full; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\CADscout Light"; Filename: "{app}\CADscoutLight.exe"; Components: light
Name: "{group}\CADscout Full"; Filename: "{app}\CADscoutFull.exe"; Components: full
Name: "{autodesktop}\CADscout Light"; Filename: "{app}\CADscoutLight.exe"; Tasks: desktopicon; Components: light
Name: "{autodesktop}\CADscout Full"; Filename: "{app}\CADscoutFull.exe"; Tasks: desktopicon; Components: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\CADscoutLight.exe"; Description: "Launch CADscout Light"; Flags: postinstall nowait skipifsilent; Check: IsLightType
Filename: "{app}\CADscoutFull.exe"; Description: "Launch CADscout Full"; Flags: postinstall nowait skipifsilent; Check: IsFullType

[Code]
function IsLightType: Boolean;
begin
  Result := WizardSetupType(False) = 'light';
end;

function IsFullType: Boolean;
begin
  Result := WizardSetupType(False) = 'full';
end;
