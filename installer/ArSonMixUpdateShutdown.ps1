# SPDX-License-Identifier: AGPL-3.0-only
# Inno-only preflight for UPGRADES FROM OLDER ARSONMIX VERSIONS.
# Never ships into the installed program directory. Never kills a process.
# Only sends WM_CLOSE to top-level windows whose owning process image is
# the EXACT ArSonMix.exe path being replaced. Other ArSonMix copies are safe.
param(
    [Parameter(Mandatory=$true)][string]$TargetPath,
    [ValidateRange(1,90)][int]$WaitSeconds=25
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
try {
    $target=[System.IO.Path]::GetFullPath($TargetPath).TrimEnd([char]'\')
    $name=[System.IO.Path]::GetFileNameWithoutExtension($target)
    if($name -ne 'ArSonMix'){exit 30}
    # Start with the native .NET process list, no taskkill, broad WM_CLOSE
    # broadcasts, WMI process termination, or process-name-only shutdown.
    function Get-TargetProcesses {
        $found=@()
        foreach($process in [System.Diagnostics.Process]::GetProcessesByName('ArSonMix')) {
            try {
                $path=$process.MainModule.FileName
                if([string]::IsNullOrWhiteSpace($path)){
                    throw 'No process image path'
                }
                $path=[System.IO.Path]::GetFullPath($path)
                if([string]::Equals($path,$target,[System.StringComparison]::OrdinalIgnoreCase)) {
                    $found+=,$process
                } else {
                    $process.Dispose()
                }
            } catch {
                # Windows may restrict MainModule even for a readable name.
                # Do not guess identity and NEVER close by process name only.
                $process.Dispose()
            }
        }
        return @($found)
    }
    $matches=@(Get-TargetProcesses)
    if($matches.Count -eq 0){exit 0}
    # Some JUCE versions have an ownerless top-level HWND; CloseMainWindow
    # alone reports false. Match HWND -> PID before posting WM_CLOSE.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ArSonMixInstallerWindowShutdown {
    private delegate bool EnumProc(IntPtr hwnd, IntPtr unused);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern bool EnumWindows(EnumProc callback, IntPtr unused);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    public static int CloseTopLevelWindows(int targetPid) {
        int requested=0;
        EnumWindows(delegate(IntPtr hwnd, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(hwnd,out owner);
            if(owner==(uint)targetPid && PostMessage(hwnd,0x0010,IntPtr.Zero,IntPtr.Zero))
                ++requested;
            return true;
        },IntPtr.Zero);
        return requested;
    }
}
'@ -ErrorAction Stop | Out-Null
    foreach($process in $matches) {
        try {
            # JUCE handles WM_CLOSE on its message thread and can save session.
            $null=$process.CloseMainWindow()
            $null=[ArSonMixInstallerWindowShutdown]::CloseTopLevelWindows($process.Id)
        } finally {$process.Dispose()}
    }
    $end=[DateTime]::UtcNow.AddSeconds($WaitSeconds)
    do {
        Start-Sleep -Milliseconds 125
        $live=@(Get-TargetProcesses)
        $count=$live.Count
        foreach($process in $live){$process.Dispose()}
        if($count -eq 0){exit 0}
    } while([DateTime]::UtcNow -lt $end)
    # A modal unsaved-edit prompt or hung mixer must remain untouched.
    # Installer aborts BEFORE opening the target EXE for replacement.
    exit 21
} catch {
    # Fail closed: the installer will not attempt to replace a possibly
    # locked executable after an unverified shutdown operation.
    exit 22
}
