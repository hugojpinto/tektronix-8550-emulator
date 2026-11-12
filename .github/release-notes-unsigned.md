<!-- Appended to every release's notes by .github/workflows/release.yml. -->
## These binaries are not signed or notarised

The programs are not signed with an Apple Developer ID or notarised by Apple, and not Authenticode-signed for Windows. Both systems will warn the first time you run them. That is expected; until signed builds are available, here is how to run them.

**macOS.** Gatekeeper blocks programs downloaded from the internet that are not notarised. It may say the program "cannot be opened because the developer cannot be verified", or that it "is damaged". Clear the download's quarantine flag once, in Terminal, inside the unpacked folder:
```
cd ~/Downloads/<the unpacked folder>
xattr -dr com.apple.quarantine .
```
Alternatively, run a program once, then open **System Settings → Privacy & Security** and click **Open Anyway**. This has to be done for each program.

**Windows.** SmartScreen may show "Windows protected your PC". Click **More info → Run anyway**. To avoid it for every file, unblock the zip *before* extracting it: right-click it, choose **Properties**, tick **Unblock**, then **OK**. In PowerShell: `Unblock-File .\<the zip file>`. Some antivirus programs are also warier of unsigned executables.

**Linux** needs nothing.
