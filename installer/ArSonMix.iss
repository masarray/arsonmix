; ArSonMix x64 per-user Inno Setup: safe in-place upgrade + single launch.
; Exact independently verified FAT app; no new DSP or GUI.
#ifndef SourceRoot
  #error "Supply /DSourceRoot with SHA-verified staging directory"
#endif
#ifndef OutputRoot
  #error "Supply /DOutputRoot with destination directory"
#endif
#ifndef SourceAppVersion
  #error "Supply /DSourceAppVersion from the authoritative CMake project version"
#endif
[Setup]
AppId=ArSonMix.LiveStudio
AppName=ArSonMix
AppVersion={#SourceAppVersion}
AppVerName=ArSonMix {#SourceAppVersion} Preview
AppPublisher=ArSonMix
AppPublisherURL=https://masarray.github.io/arsonmix/
DefaultDirName={localappdata}\Programs\ArSonMix
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
OutputDir={#OutputRoot}
OutputBaseFilename=ArSonMix-Windows-x64-Setup-Preview
SetupIconFile={#SourceRoot}\arsonmix-app.ico
UninstallDisplayIcon={app}\ArSonMix.exe
Compression=lzma2
SolidCompression=yes
UsePreviousAppDir=yes
DisableWelcomePage=yes
DisableDirPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
SetupMutex=ArSonMix.PerUserInstaller.SingleInstance
; Our pre-install shutdown owns the lifecycle for BOTH new and legacy
; releases. Do not let Windows Restart Manager issue a second shutdown or
; force a hung mixer after our fail-closed check. A single controlled
; WM_CLOSE followed by full process exit is required before replacing EXE.
CloseApplications=no
RestartApplications=no
[Files]
; The shutdown helper is EMBEDDED TEMPORARILY in Setup, never installed.
; It handles installed legacy builds that do not implement the new event.
Source: "ArSonMixUpdateShutdown.ps1"; Flags: dontcopy
Source: "{#SourceRoot}\verified-gui.exe"; DestDir: "{app}"; DestName: "ArSonMix.exe"; Flags: ignoreversion
Source: "{#SourceRoot}\BUILD_PROVENANCE.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\FactoryFX\*"; DestDir: "{app}\FactoryFX"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
[Icons]
Name: "{autoprograms}\ArSonMix"; Filename: "{app}\ArSonMix.exe"; WorkingDir: "{app}"
; Do not create a second copy of an already running mixer. Restart Manager
; preflight closes the prior verified app before [Files]; [Run] launches exactly
; one new executable only after a successful interactive install/update.
; Silent enterprise/FAT installs deliberately do not launch desktop audio.
[Run]
Filename: "{app}\ArSonMix.exe"; WorkingDir: "{app}"; Flags: nowait runasoriginaluser skipifsilent

; Never delete private Studio data on install/update/uninstall; never install
; portable_mode.txt or silently ARM an audio output.

; Upgrade preflight operates BEFORE [Files]: save+close native apps, support
; legacy versions with no shutdown handshake, verify the EXE is replaceable.
; Never force-terminate, never skip an occupied file, and never erase Studio.
[Code]
function OpenEventW(Access: LongWord; InheritHandle: Boolean; Name: string): THandle;
  external 'OpenEventW@kernel32.dll stdcall';
function SignalEvent(EventHandle: THandle): Boolean;
  external 'SetEvent@kernel32.dll stdcall';
function CloseWinHandle(EventHandle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';
function OpenExclusiveFile(Path: string; DesiredAccess, ShareMode: LongWord;
  SecurityAttributes: LongWord; CreationDisposition, Flags: LongWord;
  Template: THandle): THandle;
  external 'CreateFileW@kernel32.dll stdcall';

function InstalledExeReplaceable(Path: string): Boolean;
var
  H: THandle;
begin
  if not FileExists(Path) then
  begin
    Result := True;
    Exit;
  end;
  { Require read/write on the target and forbid sharing while we check.
    A loaded image, other process handle, filesystem permissions or AV
    quarantine must prevent installation before any user files are changed. }
  H := OpenExclusiveFile(Path, $C0000000, 0, 0, 3, $80, 0);
  Result := H <> THandle(-1);
  if Result then
    CloseWinHandle(H);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Target, HelperPath, PowerShellExe, Params: string;
  ShutdownSignal: THandle;
  Attempt, ExitCode: Integer;
begin
  Result := '';
  Target := ExpandConstant('{app}\ArSonMix.exe');
  if not FileExists(Target) then
    Exit; { first install: there is no existing exe to close }

  { First attempt: updated app receives its OWN graceful GUI-thread quit
    event. Never signal on the audio callback or issue terminate/kill. }
  ShutdownSignal := OpenEventW($0002, False,
    'Local\ArSonMix.InstallShutdown.v1');
  if ShutdownSignal <> 0 then
  begin
    if not SignalEvent(ShutdownSignal) then
    begin
      CloseWinHandle(ShutdownSignal);
      Result := 'ArSonMix could not begin its safe session shutdown. Close the mixer normally and run Setup again. Your Studio files were not changed.';
      Exit;
    end;
    CloseWinHandle(ShutdownSignal);
    for Attempt := 1 to 100 do
    begin
      if not CheckForMutexes('Local\ArSonMix.MixerRunning.v1') then
        Break;
      Sleep(200);
    end;
  end;

  { Critical legacy upgrade path: older ArSonMix versions do not own the
    event or mutex above. Locate ONLY processes executing the exact
    installation path, request WM_CLOSE and wait for the exit of ALL of them.
    Doing this explicitly avoids showing Inno's late DeleteFile code 5 UI. }
  ExtractTemporaryFile('ArSonMixUpdateShutdown.ps1');
  HelperPath := ExpandConstant('{tmp}\ArSonMixUpdateShutdown.ps1');
  PowerShellExe := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    AddQuotes(HelperPath) + ' -TargetPath ' + AddQuotes(Target) +
    ' -WaitSeconds 25';
  ExitCode := -1;
  if not Exec(PowerShellExe, Params, '', SW_HIDE,
      ewWaitUntilTerminated, ExitCode) then
  begin
    Result := 'Unable to check the running ArSonMix process before update. Close ArSonMix normally and run Setup again. No program files were replaced.';
    Exit;
  end;
  if ExitCode <> 0 then
  begin
    Log('ArSonMix legacy close preflight failed, helper exit=' + IntToStr(ExitCode));
    Result := 'ArSonMix is still running or its shutdown could not be verified. Close the mixer normally, save the session, then run Setup again. No program files were replaced.';
    Exit;
  end;

  { An unrelated process (AV, backup, another plugin host) can still hold
    the file even after ArSonMix exits. Test an EXCLUSIVE writable handle
    before Inno begins updating anything. Fail closed, no Skip/Retry UI. }
  if not InstalledExeReplaceable(Target) then
    Result := 'ArSonMix.exe is still locked or Windows does not permit replacement. Close any running mixer, check Windows access permissions and retry Setup. Your Studio data and existing executable were not modified.';
end;
