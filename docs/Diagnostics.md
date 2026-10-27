# Diagnostics with RedMonitor

RedXe sends bounded diagnostics labelled RedXe through the shared RedMonitor client header.
Install the production Release helper independently from its standalone package:
run Install-RedMonitor.ps1 in the extracted RedMonitor ZIP (CurrentUser is the default).
It installs under LocalAppData/Programs/RedMonitor; LocalMachine selects ProgramFiles/RedMonitor.

Start RedMonitor separately, or use RedXe.exe --redmonitor. A bare Debug launch may also start the
installed Release helper asynchronously. Release apps, help, tests and probes never auto-start it.
The application keeps working when no viewer is installed or ETW permissions prevent collection.
An explicit --redmonitor request returns a failure code when the helper is absent.

Ordinary RedXe builds compile only the pinned Client.h and verify local hashes; they never rebuild or
package the viewer. Developers update the client with Update-RedMonitorClient.ps1 -Commit FULL_SHA,
then review and commit the header, MIT notice and lock. No sibling checkout is needed.
Viewer settings belong to RedMonitor. Existing application logs, crash recovery and preferences retain
their ownership. ETW access may require Performance Log Users membership or elevation.