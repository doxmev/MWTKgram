; MWTKgram installer (Inno Setup 6).
; Installs for the current user into %APPDATA%\MWTKgram, so no admin rights
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
AppId={{6B0C3E52-6F0F-4C4E-9D2C-7E1C9A4D3B11}
AppName=MWTKgram
AppVersion={#AppVersion}
AppVerName=MWTKgram (сборка {#AppVersion})
AppPublisher=MWTKgram
AppPublisherURL=https://github.com/doxmev/MWTKgram
DefaultDirName={userappdata}\MWTKgram
DisableDirPage=yes
DefaultGroupName=MWTKgram
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=MWTKgram-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\MWTKgram.exe
UninstallDisplayName=MWTKgram
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
Source: "{#SourceDir}\MWTKgram.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\MWTKgram"; Filename: "{app}\MWTKgram.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\MWTKgram"; Filename: "{app}\MWTKgram.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\MWTKgram.exe"; Description: "Запустить MWTKgram"; WorkingDir: "{app}"; Flags: nowait postinstall

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
    'Был ли MWTKgram у тебя раньше?',
    'Если ты уже запускал MWTKgram.exe из какой-то папки (например, с рабочего стола), ' +
    'выбери эту папку — вход в аккаунт и настройки перенесутся. ' +
    'Если клиента раньше не было, просто нажми «Далее».',
    False, '');
  OldDirPage.Add('Папка, где лежал старый MWTKgram.exe:');
  Initial := ExpandConstant('{param:OLDDIR|}');
  if Initial = '' then
    Initial := ExpandConstant('{userdesktop}');
  OldDirPage.Values[0] := Initial;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = OldDirPage.ID) and AlreadyHasData();
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
