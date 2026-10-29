# Diagnostics with RedMonitor

RedXe sends bounded diagnostics labelled RedXe through the shared RedMonitor client header.
Install the production Release viewer separately; RedXe never builds, packages or installs it.
RedMonitor has no published release yet: build its package from
[its repository](https://github.com/RedSalamanders/RedMonitor) with `./package.ps1 -Configuration Release`,
then run Install-RedMonitor.ps1 in the extracted ZIP (CurrentUser is the default).
It installs under LocalAppData/Programs/RedMonitor; LocalMachine selects ProgramFiles/RedMonitor.

Start RedMonitor separately, or use RedXe.exe --redmonitor. A bare Debug launch may also start the
installed Release helper asynchronously. Release apps, help, tests and probes never auto-start it.
The application keeps working when no viewer is installed or ETW permissions prevent collection.
`--redmonitor` exits 1 when no Release viewer is installed or it cannot start, and says which folders it
searched or why the launch failed: on the console it was started from, or in a message box without one.
When the viewer is already running, RedXe leaves that instance as it is and exits 0.

Ordinary RedXe builds compile only the pinned Client.h and verify local hashes; they never rebuild or
package the viewer. Developers update the client with Update-RedMonitorClient.ps1 -Commit FULL_SHA,
then review and commit the header, MIT notice and lock. No sibling checkout is needed.
Viewer settings belong to RedMonitor. Existing application logs, crash recovery and preferences retain
their ownership. ETW access may require Performance Log Users membership or elevation.