; RASPIgram installer (Inno Setup 6).
; Installs for the current user into %APPDATA%\RASPIgram, so no admin rights
; are needed and the chats (tdata) live next to the program, not on the
; desktop. The in-app updater runs this file with /SILENT /OLDDIR=<folder>.

#ifndef AppVersion
  #define AppVersion "0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\..\artifact"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\installer"
#endif

[Setup]
AppId={{B696DCC4-9232-405E-862B-34AEF711595C}
AppName=RASPIgram
AppVersion={#AppVersion}
AppVerName=RASPIgram (сборка {#AppVersion})
AppPublisher=RASPIgram
AppPublisherURL=https://github.com/doxmev/RASPIgram
DefaultDirName={userappdata}\RASPIgram
DisableDirPage=yes
DefaultGroupName=RASPIgram
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=RASPIgram-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\RASPIgram.exe
UninstallDisplayName=RASPIgram
CloseApplications=force
RestartApplications=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Ярлык на рабочем столе"; GroupDescription: "Ярлыки:"

[Files]
Source: "{#SourceDir}\RASPIgram.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\RASPIgram"; Filename: "{app}\RASPIgram.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\RASPIgram"; Filename: "{app}\RASPIgram.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\RASPIgram.exe"; Description: "Запустить RASPIgram"; WorkingDir: "{app}"; Flags: nowait postinstall

[UninstallDelete]
Type: filesandordirs; Name: "{app}\DebugLogs"
Type: filesandordirs; Name: "{app}\tupdates"

[Code]
var
  OldDirPage: TInputDirWizardPage;

function AlreadyHasData(): Boolean;
begin
  Result := DirExists(ExpandConstant('{app}\tdata'));
end;

procedure InitializeWizard();
var
  Initial: String;
begin
  OldDirPage := CreateInputDirPage(wpSelectTasks,
    'Перенос данных',
    'Был ли RASPIgram у тебя раньше?',
    'Если ты уже запускал RASPIgram.exe из какой-то папки (например, с рабочего стола), ' +
    'выбери эту папку — вход в аккаунт и настройки перенесутся. ' +
    'Если клиента раньше не было, просто нажми «Далее».',
    False, '');
  OldDirPage.Add('Папка, где лежал старый RASPIgram.exe:');
  Initial := ExpandConstant('{param:OLDDIR|}');
  if Initial = '' then
    Initial := ExpandConstant('{userdesktop}');
  OldDirPage.Values[0] := Initial;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // A new client: nothing to carry over unless the updater passed /OLDDIR.
  Result := (PageID = OldDirPage.ID)
    and (AlreadyHasData() or (ExpandConstant('{param:OLDDIR|}') = ''));
end;

procedure MigrateOldData();
var
  Source, Target: String;
  Code: Integer;
begin
  if AlreadyHasData() then
    Exit;
  Source := OldDirPage.Values[0];
  if (Source = '') or not FileExists(Source + '\tdata\key_datas') then
    Exit;
  Target := ExpandConstant('{app}\tdata');
  if CompareText(RemoveBackslashUnlessRoot(Source), ExpandConstant('{app}')) = 0 then
    Exit;
  Exec(ExpandConstant('{sys}\robocopy.exe'),
    '"' + Source + '\tdata" "' + Target + '" /E /R:1 /W:1 /NFL /NDL /NJH /NJS',
    '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    MigrateOldData();
end;
