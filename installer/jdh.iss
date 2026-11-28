; JDH 설치 프로그램 (Inno Setup 6) — 사용자 계정에 설치 (관리자 권한 없이)
[Setup]
AppId={{6B1E6C0E-5E0B-4B1B-9C0E-4A44483D4831}
AppName=JDH (JawBreaker Development Helper)
AppVersion=0.7.0
DefaultDirName={localappdata}\Programs\JDH
DefaultGroupName=JawBreaker
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=out
OutputBaseFilename=JDH-Setup-0.7.0
Compression=lzma2
SolidCompression=yes
ChangesEnvironment=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Files]
Source: "..\dist\bin\jdh.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\dist\bin\jbdev.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\dist\editor\jbdev.html"; DestDir: "{app}\editor"; Flags: ignoreversion
Source: "..\dist\editor\native\*"; DestDir: "{app}\editor\native"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\dist\sdk\html\*"; DestDir: "{app}\sdk\html"; Flags: ignoreversion
Source: "..\dist\sdk\jdh.h"; DestDir: "{app}\sdk"; Flags: ignoreversion
Source: "..\dist\sdk\runtime\*"; DestDir: "{app}\sdk\runtime"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\dist\sdk\SDL2\*"; DestDir: "{app}\sdk\SDL2"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\dist\toolchain\zig\*"; DestDir: "{app}\toolchain\zig"; Flags: ignoreversion recursesubdirs createallsubdirs
; 설치된 파일 목록 (jdh update 가 지난 버전의 파일을 정리할 때 씀 · CI 가 만듦)
Source: "..\dist\jdh-manifest.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[UninstallDelete]
; jdh update 가 넣은 파일은 설치 기록에 없으므로 폴더째 지움
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\editor"
Type: filesandordirs; Name: "{app}\sdk"
Type: filesandordirs; Name: "{app}\toolchain"
Type: filesandordirs; Name: "{app}\.jdh-update"
Type: files; Name: "{app}\jdh-manifest.txt"

[Icons]
Name: "{group}\JawBreaker Dev."; Filename: "{app}\bin\jbdev.exe"
Name: "{group}\JDH 명령 창"; Filename: "{cmd}"; Parameters: "/k ""set PATH={app}\bin;%PATH% && jdh"""; WorkingDir: "{userdocs}"
Name: "{userdesktop}\JawBreaker Dev."; Filename: "{app}\bin\jbdev.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "바탕 화면에 JawBreaker Dev. 바로 가기 만들기"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: string; ValueName: "JDH_HOME"; ValueData: "{app}"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\bin\jbdev.exe"; Description: "JawBreaker Dev. 실행"; Flags: nowait postinstall skipifsilent

[Code]
// 사용자 PATH 에 {app}\bin 을 더하고, 제거할 때 뺌
procedure AddToPath(Dir: string);
var P: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', P) then P := '';
  if Pos(';' + Lowercase(Dir) + ';', ';' + Lowercase(P) + ';') > 0 then exit;
  if (P <> '') and (P[Length(P)] <> ';') then P := P + ';';
  RegWriteExpandStringValue(HKCU, 'Environment', 'Path', P + Dir);
end;
procedure RemoveFromPath(Dir: string);
var P: string; I: Integer;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', P) then exit;
  I := Pos(';' + Lowercase(Dir) + ';', ';' + Lowercase(P) + ';');
  if I = 0 then exit;
  Delete(P, I, Length(Dir) + 1);
  if (P <> '') and (P[Length(P)] = ';') then Delete(P, Length(P), 1);
  RegWriteExpandStringValue(HKCU, 'Environment', 'Path', P);
end;
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then AddToPath(ExpandConstant('{app}\bin'));
end;
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then RemoveFromPath(ExpandConstant('{app}\bin'));
end;
