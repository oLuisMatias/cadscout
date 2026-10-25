#define PackageRoot AddBackslash(SourcePath) + "..\build\package"

; The version comes from build_all.bat (/DAppVersion=...), which reads it from vcpkg.json.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{A0C3CF48-9BE9-4D22-BD4F-8E8977EA62D5}
AppName=STEP Viewer
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\STEP Viewer
DefaultGroupName=STEP Viewer
OutputDir=..\build
OutputBaseFilename=StepViewerSetup
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayIcon={app}\StepViewerLight.exe

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
Name: "{group}\STEP Viewer Light"; Filename: "{app}\StepViewerLight.exe"; Components: light
Name: "{group}\STEP Viewer Full"; Filename: "{app}\StepViewerFull.exe"; Components: full
Name: "{autodesktop}\STEP Viewer Light"; Filename: "{app}\StepViewerLight.exe"; Tasks: desktopicon; Components: light
Name: "{autodesktop}\STEP Viewer Full"; Filename: "{app}\StepViewerFull.exe"; Tasks: desktopicon; Components: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\StepViewerLight.exe"; Description: "Launch STEP Viewer Light"; Flags: postinstall nowait skipifsilent; Check: IsLightType
Filename: "{app}\StepViewerFull.exe"; Description: "Launch STEP Viewer Full"; Flags: postinstall nowait skipifsilent; Check: IsFullType

[Code]
function IsLightType: Boolean;
begin
  Result := WizardSetupType(False) = 'light';
end;

function IsFullType: Boolean;
begin
  Result := WizardSetupType(False) = 'full';
end;
