// SPDX-License-Identifier: AGPL-3.0-only
// Windows FAT-only test double for the installed ArSonMix.exe.
// Never shipped. Observe Restart Manager's Windows session-close protocol,
// not just FormClosing: RM may send WM_QUERYENDSESSION/WM_ENDSESSION before
// it ever sends WM_CLOSE. Test only passes when a durable marker was written
// by one of those cooperative message handlers AND the process has exited.
using System;
using System.IO;
using System.Windows.Forms;

internal static class ArSonMixInstallerGracefulCloseFixture
{
    private sealed class MixerWindow : Form
    {
        private readonly string savedMarker;
        private readonly bool blockClose;
        private readonly System.Threading.Mutex runningMutex;
        private readonly System.Threading.EventWaitHandle shutdownEvent;
        private readonly System.Windows.Forms.Timer shutdownPoll;
        private const int WM_QUERYENDSESSION = 0x0011;
        private const int WM_ENDSESSION = 0x0016;

        public MixerWindow(string marker, bool legacy, bool blockClose)
        {
            savedMarker = marker;
            this.blockClose = blockClose;
            Text = "ArSonMix Upgrade Graceful-Close FAT Fixture";
            Width = 420;
            Height = 120;
            ShowInTaskbar = false;
            // The production mixer creates exactly these same per-session
            // handles. Inno signals the event and waits for the mutex to go
            // away after the accepted mixer session has been saved.
            if (!legacy)
            {
                runningMutex = new System.Threading.Mutex(false,
                    @"Local\ArSonMix.MixerRunning.v1");
                shutdownEvent = new System.Threading.EventWaitHandle(false,
                    System.Threading.EventResetMode.ManualReset,
                    @"Local\ArSonMix.InstallShutdown.v1");
                shutdownPoll = new System.Windows.Forms.Timer();
                shutdownPoll.Interval = 50;
                shutdownPoll.Tick += (sender, e) =>
                {
                    if (!shutdownEvent.WaitOne(0)) return;
                    shutdownPoll.Stop();
                    File.WriteAllText(savedMarker, "INSTALLER_HANDSHAKE_SAVED");
                    Close();
                };
                shutdownPoll.Start();
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            if (shutdownPoll != null) { shutdownPoll.Stop(); shutdownPoll.Dispose(); }
            if (shutdownEvent != null) shutdownEvent.Dispose();
            if (runningMutex != null) runningMutex.Dispose();
            base.OnFormClosed(e);
        }

        protected override void WndProc(ref Message m)
        {
            // JUCE's Windows peer dispatches WM_QUERYENDSESSION to
            // JUCEApplication::systemRequestedQuit(), which saves the mixer
            // when ConsoleWindow is destructed. Simulate the durable save
            // at the corresponding OS request, not an abrupt termination.
            if (blockClose && (m.Msg == WM_QUERYENDSESSION || m.Msg == WM_ENDSESSION))
            {
                m.Result = IntPtr.Zero; // Test only: reject OS close as well as WM_CLOSE.
                return;
            }
            if (m.Msg == WM_QUERYENDSESSION)
                File.WriteAllText(savedMarker, "RM_QUERY_SAVED");
            else if (m.Msg == WM_ENDSESSION && m.WParam != IntPtr.Zero)
                File.WriteAllText(savedMarker, "RM_ENDSESSION_SAVED");
            base.WndProc(ref m);
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (blockClose) { e.Cancel = true; return; }
            if (!File.Exists(savedMarker))
                File.WriteAllText(savedMarker, "WM_CLOSE_SAVED");
            base.OnFormClosing(e);
        }
    }

    [STAThread]
    private static void Main(string[] args)
    {
        if (args.Length != 2 && args.Length != 3)
            Environment.Exit(22);
        bool legacy = args.Length == 3 && (args[2] == "legacy" || args[2] == "blocked");
        bool blockClose = args.Length == 3 && args[2] == "blocked";
        if (blockClose)
        {
            // Test-only locked EXE with no usable GUI window: WM_CLOSE can
            // never be acknowledged. The installer MUST abort before [Files].
            File.WriteAllText(args[0], "WINDOW_READY_BLOCKED");
            System.Threading.Thread.Sleep(System.Threading.Timeout.Infinite);
            return;
        }

        using (var window = new MixerWindow(args[1], legacy, blockClose))
        {
            window.Shown += (sender, e) => File.WriteAllText(args[0],
                blockClose ? "WINDOW_READY_BLOCKED" : legacy ? "WINDOW_READY_LEGACY" : "WINDOW_READY");
            Application.Run(window);
        }
    }
}
