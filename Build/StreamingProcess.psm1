Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Runs a child process for build.ps1, test.ps1 and the package smoke: streams both outputs line by line to a log and
# a callback, and bounds a child with a budget inside a kill-on-close job. See Specs/Build/Build_Process.md for the
# contract this module implements.

function ConvertTo-RedXeQuotedProcessArgument {
    param(
        [AllowNull()]
        [string] $Argument
    )

    if ($null -eq $Argument -or $Argument.Length -eq 0) {
        return '""'
    }
    if ($Argument -notmatch '[\s"]') {
        return $Argument
    }

    $builder = [Text.StringBuilder]::new()
    [void] $builder.Append('"')
    $backslashCount = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashCount
            continue
        }
        if ($character -eq '"') {
            if ($backslashCount -gt 0) {
                [void] $builder.Append(('\' * ($backslashCount * 2)))
                $backslashCount = 0
            }
            [void] $builder.Append('\"')
            continue
        }
        if ($backslashCount -gt 0) {
            [void] $builder.Append(('\' * $backslashCount))
            $backslashCount = 0
        }
        [void] $builder.Append($character)
    }
    if ($backslashCount -gt 0) {
        [void] $builder.Append(('\' * ($backslashCount * 2)))
    }
    [void] $builder.Append('"')
    return $builder.ToString()
}

# The command line of both start paths, so they cannot quote differently: each argument quoted as
# ProcessStartInfo.ArgumentList quotes it, which a child's C runtime parses back into the same argv. A loop, not the
# pipeline, so $null is no argument, as an empty list is, rather than one empty argument.
function ConvertTo-RedXeProcessCommandLine {
    param(
        [string[]] $Arguments = @()
    )

    return (@(foreach ($argument in $Arguments) {
        ConvertTo-RedXeQuotedProcessArgument -Argument $argument
    }) -join ' ')
}

# A Windows job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE that starts the bounded child itself, so the child and
# every process it starts end together: Terminate() at a budget, and Dispose() (the last handle closing) for whatever
# is left. Start() creates the child suspended and resumes it only once it belongs to the job, so no process the child
# creates can come into being outside the job. Nothing launched independently can be in this job. The job also sets
# JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION: an unhandled exception in any of its processes ends that process
# instead of holding it in a Windows Error Reporting dialog until the budget runs out, even in a child that never calls
# Common/FailureReports.h (RedXe.exe's crash harness and help runs) and whatever error mode it inherited.
# A session cannot unload a compiled type, so the namespace carries a digest of this source: an edited definition
# compiles under new type names, and a session that loaded an earlier one never keeps running it.
$script:ContainmentJobSource = @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;
namespace RedXe.Build
{
    public sealed class ContainmentJob : IDisposable
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimits info, int size);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateJobObject(IntPtr job, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool QueryInformationJobObject(IntPtr job, int infoClass, out BasicAccounting info, int size, IntPtr returnLength);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CreatePipe(out SafeFileHandle readPipe, out SafeFileHandle writePipe, IntPtr attributes, int size);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetHandleInformation(SafeFileHandle handle, uint mask, uint flags);
        [DllImport("kernel32.dll")]
        static extern IntPtr GetStdHandle(int standardHandle);
        [DllImport("kernel32.dll")]
        static extern uint GetConsoleOutputCP();
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcessW(string applicationName, StringBuilder commandLine, IntPtr processAttributes,
            IntPtr threadAttributes, bool inheritHandles, uint creationFlags, IntPtr environment, string currentDirectory,
            ref StartupInfo startupInfo, out ProcessInformation processInformation);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [StructLayout(LayoutKind.Sequential)]
        struct BasicLimits
        {
            public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
            public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct BasicAccounting
        {
            public long TotalUserTime, TotalKernelTime, ThisPeriodTotalUserTime, ThisPeriodTotalKernelTime;
            public uint TotalPageFaultCount, TotalProcesses, ActiveProcesses, TotalTerminatedProcesses;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct IoCounters { public ulong Read, Write, Other, ReadBytes, WriteBytes, OtherBytes; }
        [StructLayout(LayoutKind.Sequential)]
        struct ExtendedLimits
        {
            public BasicLimits Basic; public IoCounters Io;
            public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct StartupInfo
        {
            public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
            public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct ProcessInformation { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
        const int JobObjectBasicAccountingInformation = 1, JobObjectExtendedLimitInformation = 9;
        const uint JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION = 0x400, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000;
        const uint HANDLE_FLAG_INHERIT = 0x1;
        const int STD_INPUT_HANDLE = -10;
        const int STARTF_USESTDHANDLES = 0x100;
        const uint CREATE_SUSPENDED = 0x4, CREATE_NO_WINDOW = 0x08000000;
        // Only this class's own starts are serialized; the child ends are inheritable just for one CreateProcess.
        static readonly object StartLock = new object();
        IntPtr handle;
        public ContainmentJob()
        {
            handle = CreateJobObjectW(IntPtr.Zero, null);
            if (handle == IntPtr.Zero) throw new Win32Exception();
            var limits = new ExtendedLimits();
            limits.Basic.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
            if (!SetInformationJobObject(handle, JobObjectExtendedLimitInformation, ref limits, Marshal.SizeOf(typeof(ExtendedLimits))))
            {
                var error = new Win32Exception(); CloseHandle(handle); handle = IntPtr.Zero; throw error;
            }
        }
        // Starts the child the way Process.Start does with UseShellExecute off, CreateNoWindow, and both output
        // streams redirected (quoted file name first, inherited environment and standard input, output decoded with
        // the console output code page, standard error with standardErrorEncoding instead when one is given, as
        // ProcessStartInfo.StandardErrorEncoding does), except that the child joins this job before its first
        // instruction runs.
        public ContainedProcess Start(string fileName, string arguments, string workingDirectory, Encoding standardErrorEncoding)
        {
            string file = fileName.Trim();
            var commandLine = new StringBuilder();
            if (file.Length > 1 && file[0] == '"' && file[file.Length - 1] == '"') commandLine.Append(file);
            else commandLine.Append('"').Append(file).Append('"');
            if (!string.IsNullOrEmpty(arguments)) commandLine.Append(' ').Append(arguments);

            SafeFileHandle outputRead = null, outputWrite = null, errorRead = null, errorWrite = null;
            ProcessInformation info;
            try
            {
                if (!CreatePipe(out outputRead, out outputWrite, IntPtr.Zero, 0)) throw new Win32Exception();
                if (!CreatePipe(out errorRead, out errorWrite, IntPtr.Zero, 0)) throw new Win32Exception();
                var startup = new StartupInfo();
                startup.cb = Marshal.SizeOf(typeof(StartupInfo));
                startup.dwFlags = STARTF_USESTDHANDLES;
                startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                startup.hStdOutput = outputWrite.DangerousGetHandle();
                startup.hStdError = errorWrite.DangerousGetHandle();
                lock (StartLock)
                {
                    if (!SetHandleInformation(outputWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
                        !SetHandleInformation(errorWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
                        throw new Win32Exception();
                    bool created = CreateProcessW(null, commandLine, IntPtr.Zero, IntPtr.Zero, true,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, IntPtr.Zero,
                        string.IsNullOrEmpty(workingDirectory) ? null : workingDirectory, ref startup, out info);
                    int error = Marshal.GetLastWin32Error();
                    // The child holds its own copies now; the parent's must close for end of file to arrive.
                    outputWrite.Dispose();
                    errorWrite.Dispose();
                    if (!created)
                        throw new Win32Exception(error, "Unable to start '" + fileName + "': " + new Win32Exception(error).Message);
                }
            }
            catch
            {
                if (outputRead != null) outputRead.Dispose();
                if (outputWrite != null) outputWrite.Dispose();
                if (errorRead != null) errorRead.Dispose();
                if (errorWrite != null) errorWrite.Dispose();
                throw;
            }
            try
            {
                if (!AssignProcessToJobObject(handle, info.hProcess)) throw new Win32Exception();
                if (ResumeThread(info.hThread) == uint.MaxValue) throw new Win32Exception();
            }
            catch
            {
                TerminateProcess(info.hProcess, 0xFFFFFFFF);
                CloseHandle(info.hProcess);
                outputRead.Dispose();
                errorRead.Dispose();
                throw;
            }
            finally
            {
                CloseHandle(info.hThread);
            }
            Encoding encoding;
            try { encoding = Encoding.GetEncoding((int)GetConsoleOutputCP()); }
            catch (ArgumentException) { encoding = new UTF8Encoding(false); }
            catch (NotSupportedException) { encoding = new UTF8Encoding(false); }
            return new ContainedProcess(info.hProcess, info.dwProcessId, outputRead, errorRead, encoding,
                standardErrorEncoding ?? encoding);
        }
        public void Terminate()
        {
            if (handle != IntPtr.Zero) TerminateJobObject(handle, 0xFFFFFFFF);
        }
        // TerminateJobObject only starts the termination. True once no process of the job is left, false if the
        // timeout passes first. It waits only after a termination, where the wait is short, so it checks every 10 ms;
        // with a zero timeout it is a single probe, which the exit grace uses to ask whether any process still runs.
        public bool WaitUntilEmpty(int milliseconds)
        {
            var clock = Stopwatch.StartNew();
            for (;;)
            {
                BasicAccounting info;
                if (!QueryInformationJobObject(handle, JobObjectBasicAccountingInformation, out info,
                    Marshal.SizeOf(typeof(BasicAccounting)), IntPtr.Zero))
                    throw new Win32Exception();
                if (info.ActiveProcesses == 0) return true;
                if (clock.ElapsedMilliseconds >= milliseconds) return false;
                Thread.Sleep(10);
            }
        }
        public void Dispose()
        {
            if (handle != IntPtr.Zero) { CloseHandle(handle); handle = IntPtr.Zero; }
        }
    }

    // The members Invoke-RedXeStreamingProcess uses from System.Diagnostics.Process, over a child the job started.
    public sealed class ContainedProcess : IDisposable
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr handle);
        const uint WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 0x102, INFINITE = 0xFFFFFFFF;
        IntPtr handle;
        readonly int id;
        readonly StreamReader standardOutput, standardError;
        internal ContainedProcess(IntPtr process, int processId, SafeFileHandle output, SafeFileHandle error,
            Encoding outputEncoding, Encoding errorEncoding)
        {
            handle = process;
            id = processId;
            standardOutput = new StreamReader(new FileStream(output, FileAccess.Read, 4096, false), outputEncoding, true, 4096);
            standardError = new StreamReader(new FileStream(error, FileAccess.Read, 4096, false), errorEncoding, true, 4096);
        }
        public int Id { get { return id; } }
        public StreamReader StandardOutput { get { return standardOutput; } }
        public StreamReader StandardError { get { return standardError; } }
        public bool HasExited { get { return Wait(0); } }
        public int ExitCode
        {
            get
            {
                uint code;
                if (!Wait(0)) throw new InvalidOperationException("The process has not exited.");
                if (!GetExitCodeProcess(handle, out code)) throw new Win32Exception();
                return unchecked((int)code);
            }
        }
        public bool WaitForExit(int milliseconds) { return Wait(milliseconds < 0 ? INFINITE : (uint)milliseconds); }
        bool Wait(uint milliseconds)
        {
            if (handle == IntPtr.Zero) throw new ObjectDisposedException("ContainedProcess");
            uint result = WaitForSingleObject(handle, milliseconds);
            if (result == WAIT_OBJECT_0) return true;
            if (result == WAIT_TIMEOUT) return false;
            throw new Win32Exception();
        }
        public void Dispose()
        {
            standardOutput.Dispose();
            standardError.Dispose();
            if (handle != IntPtr.Zero) { CloseHandle(handle); handle = IntPtr.Zero; }
        }
    }
}
'@
$script:ContainmentJobNamespace = 'RedXe.Build.V' + (Get-FileHash -Algorithm SHA256 -InputStream (
    [IO.MemoryStream]::new([Text.Encoding]::UTF8.GetBytes($script:ContainmentJobSource)))).Hash.Substring(0, 16)

function New-RedXeContainmentJob {
    $typeName = "$script:ContainmentJobNamespace.ContainmentJob"
    if (-not ($typeName -as [type])) {
        Add-Type -TypeDefinition $script:ContainmentJobSource.Replace(
            'namespace RedXe.Build', "namespace $script:ContainmentJobNamespace")
    }
    return New-Object -TypeName $typeName
}

function Invoke-RedXeStreamingProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $FilePath,

        [string[]] $Arguments = @(),

        [string] $WorkingDirectory = (Get-Location).Path,

        [Parameter(Mandatory)]
        [string] $LogPath,

        [scriptblock] $OutputLineCallback,

        # Total budget for the child, counted from its start on a monotonic clock. When it runs out, the child and every
        # process it started are terminated (they are descendants of this invocation, never an independently launched
        # process) and the call throws, naming the executable and the log. A child that has exited while a process it
        # started still holds its output open is ended the same way after a short grace, and the call says so. Zero
        # keeps the wait unbounded, as build.ps1 needs.
        [ValidateRange(0, 86400)]
        [int] $TimeoutSeconds = 0,

        # Receives the child's process identifier once it has started, for a caller that checks what the child
        # reported about itself (test.ps1's crash harness compares it with the crash report's ProcessId).
        [ref] $ProcessId,

        # Decodes the child's stderr with this encoding. Without it both streams are decoded with the console output
        # code page, as Process.Start decodes them, so a build tool (MSBuild, cl) reads the same on both paths.
        # RedXe's own processes write a redirected stderr as UTF-8 (Common/FailureReports.h), so test.ps1 and the
        # package smoke pass UTF-8 for them: a legacy code page would garble a report's non-ASCII text (U+0141, say).
        [Text.Encoding] $StandardErrorEncoding
    )

    $resolvedLogPath = [IO.Path]::GetFullPath($LogPath)
    $logDirectory = Split-Path -Parent $resolvedLogPath
    if (-not [string]::IsNullOrWhiteSpace($logDirectory)) {
        [void](New-Item -ItemType Directory -Path $logDirectory -Force)
    }
    # Every wait is sliced: PowerShell acts on Ctrl+C only between statements, never inside a .NET wait, so a single
    # wait on a silent child would hold the stop for the rest of its budget, or for good without one.
    $waitSliceMilliseconds = 500
    $budgetMilliseconds = [long] $TimeoutSeconds * 1000
    # How long an exited bounded child may leave its output open to a process it started, and how long a terminated
    # tree's pipes may take to deliver what is left in them.
    $exitGraceMilliseconds = 10000
    $drainMilliseconds = 5000
    # How long the pipes of an exited child may stay open without delivering a line once no process of the job runs:
    # only a handle that left the job can hold them then, and nothing else would ever end the wait.
    $orphanedOutputMilliseconds = 30000
    $hungMessage = "'$FilePath' did not finish within $TimeoutSeconds s and was terminated with its child processes (log: $resolvedLogPath)."

    $job = $null
    $process = $null
    $logWriter = $null
    try {
        $encoding = [Text.UTF8Encoding]::new($false)
        $logWriter = [IO.StreamWriter]::new($resolvedLogPath, $false, $encoding)

        $budgetClock = $null
        if ($TimeoutSeconds -gt 0) {
            # A bounded child is started by a job object with kill-on-close and belongs to it before it runs, so the
            # whole tree is contained: a descendant that inherited the redirected pipe and outlived the child is still
            # terminated at the budget and when this call returns.
            $job = New-RedXeContainmentJob
            $process = $job.Start($FilePath, (ConvertTo-RedXeProcessCommandLine -Arguments $Arguments), $WorkingDirectory,
                $StandardErrorEncoding)
            # The budget is the child's, so it starts once the child runs. The first bounded call in a session
            # compiles the job type above, which took over a second on a loaded machine, and neither that nor a slow
            # process creation may come out of the child's time. A change of the system time moves neither end.
            $budgetClock = [Diagnostics.Stopwatch]::StartNew()
        }
        else {
            $startInfo = [Diagnostics.ProcessStartInfo]::new()
            $startInfo.FileName = $FilePath
            $startInfo.Arguments = ConvertTo-RedXeProcessCommandLine -Arguments $Arguments
            $startInfo.WorkingDirectory = $WorkingDirectory
            $startInfo.UseShellExecute = $false
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            if ($null -ne $StandardErrorEncoding) {
                $startInfo.StandardErrorEncoding = $StandardErrorEncoding
            }
            $startInfo.CreateNoWindow = $true
            $process = [Diagnostics.Process]::new()
            $process.StartInfo = $startInfo
            if (-not $process.Start()) {
                throw "Unable to start '$FilePath'."
            }
        }
        if ($null -ne $ProcessId) {
            $ProcessId.Value = $process.Id
        }

        $standardOutputOpen = $true
        $standardErrorOpen = $true
        $standardOutputTask = $process.StandardOutput.ReadLineAsync()
        $standardErrorTask = $process.StandardError.ReadLineAsync()
        $failure = $null
        $drainClock = $null
        $childExitedAt = -1
        $lastOutputAt = 0

        while ($standardOutputOpen -or $standardErrorOpen) {
            $pendingTasks = [Collections.Generic.List[Threading.Tasks.Task[string]]]::new()
            if ($standardOutputOpen) {
                [void] $pendingTasks.Add($standardOutputTask)
            }
            if ($standardErrorOpen) {
                [void] $pendingTasks.Add($standardErrorTask)
            }

            # -1 when a slice passes without a line; the pending reads stay armed for the next pass.
            $completedIndex = [Threading.Tasks.Task]::WaitAny($pendingTasks.ToArray(), $waitSliceMilliseconds)
            if ($completedIndex -ge 0) {
                $completedTask = $pendingTasks[$completedIndex]
                $isError = $standardErrorOpen -and
                    [object]::ReferenceEquals($completedTask, $standardErrorTask)
                $line = $completedTask.GetAwaiter().GetResult()

                if ($null -eq $line) {
                    if ($isError) {
                        $standardErrorOpen = $false
                    }
                    else {
                        $standardOutputOpen = $false
                    }
                }
                else {
                    if ($budgetClock) {
                        $lastOutputAt = $budgetClock.ElapsedMilliseconds
                    }
                    $logWriter.WriteLine($line)
                    if ($OutputLineCallback) {
                        & $OutputLineCallback $line $isError
                    }
                    else {
                        Write-Host $line
                    }

                    if ($isError) {
                        $standardErrorTask = $process.StandardError.ReadLineAsync()
                    }
                    else {
                        $standardOutputTask = $process.StandardOutput.ReadLineAsync()
                    }
                }
            }

            if ($drainClock) {
                # Once the tree is terminated, its pipes deliver what is left in them, a last line without a newline
                # included, and then end.
                if ($drainClock.ElapsedMilliseconds -ge $drainMilliseconds) {
                    break
                }
            }
            elseif ($budgetClock) {
                # Checked on every pass, a line or not, so a child that never stops writing cannot outrun its budget.
                # Once the child has exited, the grace applies instead: what it left in the pipes is no hang.
                $elapsed = $budgetClock.ElapsedMilliseconds
                if ($childExitedAt -lt 0 -and $process.HasExited) {
                    $childExitedAt = $elapsed
                }
                if ($childExitedAt -ge 0) {
                    # An open pipe alone proves no descendant: a backlog the child left, or a slow callback presenting
                    # it, can outlast the grace. Only a process of the job that still runs can hold the output open;
                    # with none left, the pipes hold only what the tree wrote, and draining them reaches end of file.
                    if ($elapsed - $childExitedAt -ge $exitGraceMilliseconds -and -not $job.WaitUntilEmpty(0)) {
                        $failure = "'$FilePath' exited with code $($process.ExitCode), but a process it started kept its output open and was terminated with its own child processes (log: $resolvedLogPath)."
                    }
                    # A draining backlog keeps delivering lines. Pipes that stay open and silent with the job empty are
                    # held by a handle outside it, so the wait would never end; the budget no longer applies here.
                    elseif ($elapsed - [Math]::Max($childExitedAt, $lastOutputAt) -ge $orphanedOutputMilliseconds) {
                        $failure = "'$FilePath' exited with code $($process.ExitCode), but its output stayed open for $([int]($orphanedOutputMilliseconds / 1000)) s with no line and no process of its own left, so the wait was abandoned (log: $resolvedLogPath)."
                    }
                }
                elseif ($elapsed -ge $budgetMilliseconds) {
                    $failure = $hungMessage
                }
                if ($null -ne $failure) {
                    $job.Terminate()
                    $drainClock = [Diagnostics.Stopwatch]::StartNew()
                }
            }
        }

        if ($null -eq $failure) {
            # Both streams have ended, but the child itself may still run.
            while (-not $process.WaitForExit($waitSliceMilliseconds)) {
                if ($budgetClock -and $budgetClock.ElapsedMilliseconds -ge $budgetMilliseconds) {
                    $failure = $hungMessage
                    $job.Terminate()
                    break
                }
            }
        }
        if ($null -ne $failure) {
            # The call throws only once no process of the tree is left, so a caller that looks for survivors right
            # away finds none.
            try { [void] $job.WaitUntilEmpty(5000) } catch { }
            $logWriter.WriteLine("TIMEOUT: $failure")
            throw $failure
        }
        $exitCode = [int] $process.ExitCode
        $global:LASTEXITCODE = $exitCode
        return $exitCode
    }
    finally {
        # Nested, so a disposal that throws never skips the next one. The job goes last: closing its last handle kills
        # whatever the tree still runs (kill-on-close).
        try {
            if ($logWriter) {
                $logWriter.Dispose()
            }
        }
        finally {
            try {
                if ($process) {
                    # An unbounded child has no job. A stop (Ctrl+C) or an error before it exited ends its tree here,
                    # or MSBuild would go on building unseen. After a normal exit its descendants are left alone, as
                    # MSBuild's reusable nodes must be.
                    if ($process -is [Diagnostics.Process]) {
                        try {
                            if (-not $process.HasExited) {
                                $process.Kill($true)
                            }
                        }
                        catch { }
                    }
                    $process.Dispose()
                }
            }
            finally {
                if ($job) {
                    $job.Dispose()
                }
            }
        }
    }
}

Export-ModuleMember -Function 'Invoke-RedXeStreamingProcess'
