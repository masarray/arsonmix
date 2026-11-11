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
#ifndef VBCableSha256
  #error "Supply the verified standard VB-CABLE archive hash"
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
[Tasks]
Name: "vbcable_driver"; Description: "Install VB-CABLE driver (VB-Audio donationware, administrator approval and reboot required)"; Flags: unchecked
[Files]
; The vendor standard cable ZIP is kept AS IS, with mandatory attribution.
; Do not redistribute extra A+B / C+D driver packages.
Source: "{#SourceRoot}\VBCABLE_Driver_Pack45.zip"; DestDir: "{app}\ThirdParty\VB-Audio"; Flags: ignoreversion
Source: "{#SourceRoot}\VB-CABLE-NOTICE.txt"; DestDir: "{app}\ThirdParty\VB-Audio"; Flags: ignoreversion
Source: "ArSonMixVBCableInstall.ps1"; DestDir: "{app}\ThirdParty\VB-Audio"; Flags: ignoreversion
; The shutdown helper is EMBEDDED TEMPORARILY in Setup, never installed.
; It handles installed legacy builds that do not implement the new event.
Source: "ArSonMixUpdateShutdown.ps1"; Flags: dontcopy
; The updater runs outside JUCE, never in the realtime audio callback.
Source: "ArSonMixAutoUpdate.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\verified-gui.exe"; DestDir: "{app}"; DestName: "ArSonMix.exe"; Flags: ignoreversion
Source: "{#SourceRoot}\BUILD_PROVENANCE.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\FactoryFX\*"; DestDir: "{app}\FactoryFX"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
[Icons]
Name: "{autoprograms}\ArSonMix"; Filename: "{app}\ArSonMix.exe"; WorkingDir: "{app}"
; User-facing fallback when the main EXE cannot even open. The helper
; authenticates the published current release and repairs in place.
Name: "{autoprograms}\Repair ArSonMix"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\ArSonMixAutoUpdate.ps1"" -RecoveryMode"; WorkingDir: "{app}"
; Do not create a second copy of an already running mixer. Restart Manager
; preflight closes the prior verified app before [Files]; [Run] launches exactly
; one new executable only after a successful interactive install/update.
; Silent enterprise/FAT installs deliberately do not launch desktop audio.
[Run]
; Explicit opt-in only; the per-user Setup never obtains blanket admin rights.
; This script requests UAC only for the independently verified vendor EXE.
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoLogo -NoProfile -ExecutionPolicy Bypass -File ""{app}\ThirdParty\VB-Audio\ArSonMixVBCableInstall.ps1"" -Archive ""{app}\ThirdParty\VB-Audio\VBCABLE_Driver_Pack45.zip"" -ExpectedSha256 ""{#VBCableSha256}"""; WorkingDir: "{app}"; Description: "Optional VB-Audio VB-CABLE driver installation"; Tasks: vbcable_driver; Flags: runasoriginaluser skipifsilent waituntilterminated
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

{ An automatic update must never close a new live-mixing session.
  Manual/interactive setup retains the existing graceful-close workflow. }
function IsArSonMixAutoUpdate: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/ARSONMIXAUTOUPDATE') = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function IsArSonMixRecovery: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/ARSONMIXRECOVERY') = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Target, HelperPath, PowerShellExe, Params: string;
  ShutdownSignal: THandle;
  Attempt, ExitCode: Integer;
begin
  Result := '';
  Target := ExpandConstant('{app}\ArSonMix.exe');
  if IsArSonMixAutoUpdate then
  begin
    { Never send InstallShutdown, CloseMainWindow or WM_CLOSE in auto mode.
      Exact-path ProbeOnly + locked-PE preflight is strictly fail-closed. }
    if not FileExists(Target) and not IsArSonMixRecovery then
    begin
      Result := 'Automatic update requires an existing ArSonMix installation.';
      Exit;
    end;
    { The recovery flag is allowed only with automatic (no-close) mode.
      An absent EXE is permitted, but the same exact-image running-process
      probe and exclusive-image checks are still mandatory. }
    if CheckForMutexes('Local\ArSonMix.MixerRunning.v1') then
    begin
      Result := 'ArSonMix is running; automatic update deferred to a later normal exit.';
      Exit;
    end;
    ExtractTemporaryFile('ArSonMixUpdateShutdown.ps1');
    HelperPath := ExpandConstant('{tmp}\ArSonMixUpdateShutdown.ps1');
    PowerShellExe := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
    Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
      AddQuotes(HelperPath) + ' -TargetPath ' + AddQuotes(Target) +
      ' -ProbeOnly';
    ExitCode := -1;
    if not Exec(PowerShellExe, Params, '', SW_HIDE,
        ewWaitUntilTerminated, ExitCode) then
    begin
      Result := 'Unable to verify that no ArSonMix session is active. No files replaced.';
      Exit;
    end;
    if ExitCode <> 0 then
    begin
      Log('Automatic update refused: active/unknown process, probe exit=' + IntToStr(ExitCode));
      Result := 'ArSonMix session is active or cannot be verified. Automatic update deferred; no files replaced.';
      Exit;
    end;
    if not InstalledExeReplaceable(Target) then
      Result := 'ArSonMix.exe is occupied; automatic update deferred without closing a live session.';
    Exit;
  end;
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
