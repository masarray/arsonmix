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
        private const int WM_QUERYENDSESSION = 0x0011;
        private const int WM_ENDSESSION = 0x0016;

        public MixerWindow(string marker)
        {
            savedMarker = marker;
            Text = "ArSonMix Upgrade Graceful-Close FAT Fixture";
            Width = 420;
            Height = 120;
            ShowInTaskbar = false;
        }

        protected override void WndProc(ref Message m)
        {
            // JUCE's Windows peer dispatches WM_QUERYENDSESSION to
            // JUCEApplication::systemRequestedQuit(), which saves the mixer
            // when ConsoleWindow is destructed. Simulate the durable save
            // at the corresponding OS request, not an abrupt termination.
            if (m.Msg == WM_QUERYENDSESSION)
                File.WriteAllText(savedMarker, "RM_QUERY_SAVED");
            else if (m.Msg == WM_ENDSESSION && m.WParam != IntPtr.Zero)
                File.WriteAllText(savedMarker, "RM_ENDSESSION_SAVED");
            base.WndProc(ref m);
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            File.WriteAllText(savedMarker, "WM_CLOSE_SAVED");
            base.OnFormClosing(e);
        }
    }

    [STAThread]
    private static void Main(string[] args)
    {
        if (args.Length != 2)
            Environment.Exit(22);

        using (var window = new MixerWindow(args[1]))
        {
            window.Shown += (sender, e) => File.WriteAllText(args[0], "WINDOW_READY");
            Application.Run(window);
        }
    }
}
