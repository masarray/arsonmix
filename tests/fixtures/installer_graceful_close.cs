// SPDX-License-Identifier: AGPL-3.0-only
// Windows FAT-only test double for the installed ArSonMix.exe.
// Deliberately no JUCE / ASIO / live audio and never shipped in the installer.
// Windows Restart Manager must request WM_CLOSE rather than force-terminate it.
using System;
using System.IO;
using System.Windows.Forms;

internal static class ArSonMixInstallerGracefulCloseFixture
{
    [STAThread]
    private static void Main(string[] args)
    {
        if (args.Length != 2)
            Environment.Exit(22);

        using (var window = new Form())
        {
            window.Text = "ArSonMix Upgrade Graceful-Close FAT Fixture";
            window.Width = 420;
            window.Height = 120;
            window.ShowInTaskbar = false;
            window.Shown += (sender, e) => File.WriteAllText(args[0], "WINDOW_READY");
            window.FormClosing += (sender, e) =>
                File.WriteAllText(args[1], "WM_CLOSE_SAVED");
            Application.Run(window);
        }
    }
}
