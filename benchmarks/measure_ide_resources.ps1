param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot "..\target\x86_64-pc-windows-gnu\release\rbl-studio.exe"),
    [string]$DocumentPath = "",
    [ValidateSet("Idle", "SingleDocument", "32Tabs", "EditUndoRedo", "FindReplaceUndoRedo", "CompilerTask", "TestSuite")]
    [string]$Scenario = "Idle",
    [ValidateRange(0, 120)][int]$WarmupSeconds = 5,
    [ValidateRange(1, 300)][int]$SampleSeconds = 20,
    [ValidateRange(50, 5000)][int]$SampleIntervalMilliseconds = 250,
    [ValidateRange(1, 8192)][int]$MemoryLimitMiB = 512,
    [string]$CsvPath = ""
)

$ErrorActionPreference = "Stop"
$executable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceFiles = @("src\main.rs", "src\app.rs", "src\editor.rs", "src\process.rs", "src\settings.rs", "Cargo.toml", "Cargo.lock") |
    ForEach-Object { Join-Path $root $_ }
$sourceHashText = ($sourceFiles | ForEach-Object { (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash }) -join ""
$sha256 = [System.Security.Cryptography.SHA256]::Create()
$sourceFingerprint = [System.BitConverter]::ToString($sha256.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($sourceHashText))).Replace("-", "")
$sha256.Dispose()
$executableHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash
$cargoVersion = (Select-String -Path (Join-Path $root "Cargo.toml") -Pattern '^version\s*=\s*"([^"]+)"' | Select-Object -First 1).Matches.Groups[1].Value
$argumentLine = if ([string]::IsNullOrWhiteSpace($DocumentPath)) {
    $null
} else {
    '"' + (Resolve-Path -LiteralPath $DocumentPath).Path.Replace('"', '\"') + '"'
}
$logicalProcessors = [Environment]::ProcessorCount
$fixtureDirectory = $null
$automationProcess = $null
$automationPath = $null
$automationLogPath = $null
$automationStdoutPath = $null
$automationStderrPath = $null
$documentsFile = $null
$generatedFixtures = $false
$launchPaths = @()
if (-not [string]::IsNullOrWhiteSpace($DocumentPath)) {
    $launchPaths = @((Resolve-Path -LiteralPath $DocumentPath).Path)
}

function Write-ResourceFixture([string]$Path, [int]$SizeBytes, [string]$Pattern) {
    $patternBytes = [System.Text.Encoding]::ASCII.GetBytes($Pattern)
    $buffer = [byte[]]::new(65536)
    for ($index = 0; $index -lt $buffer.Length; $index++) {
        $buffer[$index] = $patternBytes[$index % $patternBytes.Length]
    }
    $stream = [System.IO.File]::Create($Path)
    try {
        $remaining = $SizeBytes
        while ($remaining -gt 0) {
            $count = [Math]::Min($remaining, $buffer.Length)
            $stream.Write($buffer, 0, $count)
            $remaining -= $count
        }
    }
    finally {
        $stream.Dispose()
    }
}

if ($Scenario -ne "Idle" -and [string]::IsNullOrWhiteSpace($DocumentPath)) {
    $generatedFixtures = $true
    $fixtureDirectory = Join-Path $env:TEMP ("rbl-studio-resource-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $fixtureDirectory | Out-Null
    switch ($Scenario) {
        "SingleDocument" {
            $path = Join-Path $fixtureDirectory "large-16mib.txt"
            Write-ResourceFixture $path (16 * 1MB) "RBL_RESOURCE_SAMPLE "
            $launchPaths = @($path)
        }
        "32Tabs" {
            for ($index = 0; $index -lt 32; $index++) {
                $path = Join-Path $fixtureDirectory ("tab-{0:D2}.txt" -f $index)
                Write-ResourceFixture $path (1 * 1MB) ("TAB{0:D2}_RESOURCE_SAMPLE " -f $index)
                $launchPaths += $path
            }
        }
        "EditUndoRedo" {
            for ($index = 0; $index -lt 4; $index++) {
                $path = Join-Path $fixtureDirectory ("edit-{0:D2}.txt" -f $index)
                Write-ResourceFixture $path (8 * 1MB) ("EDIT{0:D2}_RESOURCE_SAMPLE " -f $index)
                $launchPaths += $path
            }
        }
        "FindReplaceUndoRedo" {
            $path = Join-Path $fixtureDirectory "find-replace-16mib.txt"
            Write-ResourceFixture $path (16 * 1MB) "needle "
            $launchPaths = @($path)
        }
        "CompilerTask" {
            $launchPaths = @((Join-Path $root "tests\cases\gc_pressure.rbl"))
        }
        "TestSuite" { $launchPaths = @() }
    }
}

$argumentLine = if ($launchPaths.Count) {
    ($launchPaths | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
} else { $null }
$process = if ($null -eq $argumentLine) {
    Start-Process -FilePath $executable -WorkingDirectory $root -PassThru
} else {
    Start-Process -FilePath $executable -WorkingDirectory $root -ArgumentList $argumentLine -PassThru
}

if ($Scenario -in @("EditUndoRedo", "FindReplaceUndoRedo", "CompilerTask", "TestSuite")) {
    if ($null -eq $fixtureDirectory) {
        $fixtureDirectory = Join-Path $env:TEMP ("rbl-studio-resource-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $fixtureDirectory | Out-Null
    }
    $automationPath = Join-Path $fixtureDirectory "automation.ps1"
    $automationLogPath = Join-Path $fixtureDirectory "automation.log"
    $automationStdoutPath = Join-Path $fixtureDirectory "automation.stdout.log"
    $automationStderrPath = Join-Path $fixtureDirectory "automation.stderr.log"
    $documentsFile = Join-Path $fixtureDirectory "documents.txt"
    if ($launchPaths.Count) {
        [System.IO.File]::WriteAllLines($documentsFile, [string[]]$launchPaths, [System.Text.UTF8Encoding]::new($false))
    } else {
        [System.IO.File]::WriteAllText($documentsFile, "", [System.Text.UTF8Encoding]::new($false))
    }
    $automation = @'
param([int]$TargetPid, [string]$Workload, [string]$LogPath, [string]$DocumentsFile)
$ErrorActionPreference = "Stop"
function Write-Log([string]$Message) {
    Add-Content -LiteralPath $LogPath -Value ("{0:o} {1}" -f [DateTime]::UtcNow, $Message)
}
$shell = New-Object -ComObject WScript.Shell
$documentPaths = @(Get-Content -LiteralPath $DocumentsFile)
$originalSizes = @($documentPaths | ForEach-Object { (Get-Item -LiteralPath $_).Length })
Start-Sleep -Seconds 2
if (-not $shell.AppActivate($TargetPid)) { throw "Could not activate RBL Studio process $TargetPid" }
$windowProcess = [System.Diagnostics.Process]::GetProcessById($TargetPid)
$windowHandle = $windowProcess.MainWindowHandle
if ($windowHandle -eq [IntPtr]::Zero) { throw "RBL Studio process $TargetPid has no main window" }
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)] public struct RblWindowRect { public int Left, Top, Right, Bottom; }
public static class RblResourceWin32 {
    [DllImport("user32.dll", SetLastError=true)] public static extern bool GetWindowRect(IntPtr hWnd, out RblWindowRect rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
"@
$rect = New-Object RblWindowRect
if (-not [RblResourceWin32]::GetWindowRect($windowHandle, [ref]$rect)) { throw "GetWindowRect failed" }
    $foreground = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        [void]$shell.AppActivate($TargetPid)
        [void][RblResourceWin32]::ShowWindow($windowHandle, 9)
        [void][RblResourceWin32]::BringWindowToTop($windowHandle)
        [void][RblResourceWin32]::SetForegroundWindow($windowHandle)
        Start-Sleep -Milliseconds 150
        $foreground = [RblResourceWin32]::GetForegroundWindow()
        if ($foreground -eq $windowHandle) { break }
    }
    if ($foreground -ne $windowHandle) {
        throw "RBL Studio did not become the foreground window (expected=$windowHandle, actual=$foreground)"
    }
$clickX = [int]($rect.Left + (($rect.Right - $rect.Left) * 0.60))
$clickY = [int]($rect.Top + (($rect.Bottom - $rect.Top) * 0.55))
[void][RblResourceWin32]::SetCursorPos($clickX, $clickY)
[RblResourceWin32]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
[RblResourceWin32]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 250
Write-Log "Activated HWND=$windowHandle; foreground=$foreground; clicked editor-area coordinate $clickX,$clickY; bounds=$($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom)"
try {
switch ($Workload) {
    "EditUndoRedo" {
        if ($documentPaths.Count -ne 4) { throw "EditUndoRedo requires four document paths" }
        Set-Clipboard -Value ("x" * 1024)
        $shell.SendKeys("^{TAB}")
        Start-Sleep -Milliseconds 150
        for ($tab = 0; $tab -lt 4; $tab++) {
            $shell.SendKeys("^{END}")
            for ($edit = 0; $edit -lt 8; $edit++) { $shell.SendKeys("^v"); Start-Sleep -Milliseconds 80 }
            $shell.SendKeys("^s")
            Start-Sleep -Milliseconds 150
            if ((Get-Item -LiteralPath $documentPaths[$tab]).Length -le $originalSizes[$tab]) {
                throw "Paste phase did not persist on $($documentPaths[$tab])"
            }
            for ($edit = 0; $edit -lt 8; $edit++) { $shell.SendKeys("^z"); Start-Sleep -Milliseconds 40 }
            $shell.SendKeys("^s")
            Start-Sleep -Milliseconds 150
            if ((Get-Item -LiteralPath $documentPaths[$tab]).Length -ne $originalSizes[$tab]) {
                throw "Undo phase did not restore $($documentPaths[$tab])"
            }
            for ($edit = 0; $edit -lt 8; $edit++) { $shell.SendKeys("^y"); Start-Sleep -Milliseconds 40 }
            $shell.SendKeys("^s")
            Start-Sleep -Milliseconds 150
            if ((Get-Item -LiteralPath $documentPaths[$tab]).Length -le $originalSizes[$tab]) {
                throw "Redo phase did not restore the edited content in $($documentPaths[$tab])"
            }
            $shell.SendKeys("^{TAB}")
        }
    }
    "FindReplaceUndoRedo" {
        $shell.SendKeys("^h")
        Start-Sleep -Milliseconds 400
        $shell.SendKeys("needle")
        $shell.SendKeys("{TAB}")
        $shell.SendKeys("RBL")
        $shell.SendKeys("{TAB}{TAB}{TAB}{TAB}{ENTER}")
        Start-Sleep -Seconds 2
        $shell.SendKeys("{ESC}")
        for ($cycle = 0; $cycle -lt 4; $cycle++) {
            $shell.SendKeys("^z"); Start-Sleep -Milliseconds 250
            $shell.SendKeys("^y"); Start-Sleep -Milliseconds 250
        }
        $shell.SendKeys("^s")
        Start-Sleep -Seconds 1
    }
    "CompilerTask" { $shell.SendKeys("{F5}") }
    "TestSuite" { $shell.SendKeys("^t") }
}
Write-Log "Workload $Workload key sequence completed"
} catch {
    Write-Log "FAILED: $($_.Exception.Message)"
    throw
}
'@
    [System.IO.File]::WriteAllText($automationPath, $automation, [System.Text.Encoding]::UTF8)
}

$script:KnownTreeProcessIds = [System.Collections.Generic.HashSet[int]]::new()

function Get-ProcessTreeSnapshot([int]$RootPid) {
    $snapshot = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId)
    $ids = [System.Collections.Generic.HashSet[int]]::new()
    foreach ($knownId in $script:KnownTreeProcessIds) { [void]$ids.Add($knownId) }
    [void]$ids.Add($RootPid)
    $changed = $true
    while ($changed) {
        $changed = $false
        foreach ($item in $snapshot) {
            if ($ids.Contains([int]$item.ParentProcessId) -and $ids.Add([int]$item.ProcessId)) {
                $changed = $true
            }
        }
    }
    foreach ($id in $ids) { [void]$script:KnownTreeProcessIds.Add($id) }
    $members = @(Get-Process -Id @($ids) -ErrorAction SilentlyContinue)
    $root = $members | Where-Object { $_.Id -eq $RootPid } | Select-Object -First 1
    $children = @($members | Where-Object { $_.Id -ne $RootPid })
    [pscustomobject]@{
        Root = $root
        Children = $children
        Members = $members
        TreeWorkingSet = [long](($members | Measure-Object -Property WorkingSet64 -Sum).Sum)
        TreePrivateBytes = [long](($members | Measure-Object -Property PrivateMemorySize64 -Sum).Sum)
        ChildWorkingSet = [long](($children | Measure-Object -Property WorkingSet64 -Sum).Sum)
        ChildPrivateBytes = [long](($children | Measure-Object -Property PrivateMemorySize64 -Sum).Sum)
        ProcessCount = $members.Count
    }
}

try {
    Start-Sleep -Seconds ([Math]::Max(0, $WarmupSeconds))
    $process.Refresh()
    if ($process.HasExited) {
        throw "RBL Studio exited during the warm-up period (exit code $($process.ExitCode))."
    }
    if ($null -ne $automationPath) {
        $automationProcess = Start-Process -FilePath "powershell.exe" -WindowStyle Hidden -PassThru -ArgumentList @(
            "-STA", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ('"' + $automationPath + '"'),
            "-TargetPid", $process.Id, "-Workload", $Scenario,
            "-LogPath", ('"' + $automationLogPath + '"'), "-DocumentsFile", ('"' + $documentsFile + '"')
        ) -RedirectStandardOutput $automationStdoutPath -RedirectStandardError $automationStderrPath
    }

    $timeStart = Get-Date
    $workingSet = [System.Collections.Generic.List[long]]::new()
    $privateBytes = [System.Collections.Generic.List[long]]::new()
    $cpuSamples = [System.Collections.Generic.List[double]]::new()
    $treeWorkingSet = [System.Collections.Generic.List[long]]::new()
    $treePrivateBytes = [System.Collections.Generic.List[long]]::new()
    $treeCpuSamples = [System.Collections.Generic.List[double]]::new()
    $childWorkingSet = [System.Collections.Generic.List[long]]::new()
    $childPrivateBytes = [System.Collections.Generic.List[long]]::new()
    $childCpuSamples = [System.Collections.Generic.List[double]]::new()
    $processCounts = [System.Collections.Generic.List[int]]::new()
    $sampleTimes = [System.Collections.Generic.List[datetime]]::new()
    $cpuByProcess = @{}
    $treeCpuAccumulated = 0.0
    $childCpuAccumulated = 0.0
    $stopAt = $timeStart.AddSeconds($SampleSeconds)
    $sample = 0
    while ((Get-Date) -lt $stopAt -or $sample -eq 0) {
        $tree = Get-ProcessTreeSnapshot $process.Id
        if ($null -eq $tree.Root) { break }
        $tree.Root.Refresh()
        $workingSet.Add([long]$tree.Root.WorkingSet64)
        $privateBytes.Add([long]$tree.Root.PrivateMemorySize64)
        $cpuSamples.Add([double]$tree.Root.CPU)
        $treeWorkingSet.Add($tree.TreeWorkingSet)
        $treePrivateBytes.Add($tree.TreePrivateBytes)
        $childWorkingSet.Add($tree.ChildWorkingSet)
        $childPrivateBytes.Add($tree.ChildPrivateBytes)
        foreach ($member in $tree.Members) {
            $memberCpu = [double]$member.CPU
            try { $memberStartTicks = $member.StartTime.ToUniversalTime().Ticks }
            catch { $memberStartTicks = 0 }
            $memberKey = "$($member.Id):$memberStartTicks"
            $delta = 0.0
            if ($cpuByProcess.ContainsKey($memberKey)) {
                $delta = [Math]::Max(0.0, $memberCpu - [double]$cpuByProcess[$memberKey])
            } elseif ($memberStartTicks -gt $timeStart.ToUniversalTime().Ticks) {
                $delta = [Math]::Max(0.0, $memberCpu)
            }
            $cpuByProcess[$memberKey] = $memberCpu
            $treeCpuAccumulated += $delta
            if ($member.Id -ne $process.Id) {
                $childCpuAccumulated += $delta
            }
        }
        $treeCpuSamples.Add($treeCpuAccumulated)
        $childCpuSamples.Add($childCpuAccumulated)
        $processCounts.Add($tree.ProcessCount)
        $sampleTimes.Add((Get-Date))
        $sample++
        if ((Get-Date) -lt $stopAt) {
            Start-Sleep -Milliseconds $SampleIntervalMilliseconds
        }
    }
    $sampleEnd = Get-Date
    $process.Refresh()
    if ($process.HasExited) {
        throw "RBL Studio exited during the resource sampling window (exit code $($process.ExitCode))."
    }
    if ($workingSet.Count -lt 1) {
        throw "RBL Studio exited or could not be sampled."
    }
    $peakProcessCount = [int](($processCounts | Measure-Object -Maximum).Maximum)
    if ($Scenario -in @("CompilerTask", "TestSuite") -and $peakProcessCount -le 1) {
        $log = if (Test-Path -LiteralPath $automationLogPath) { Get-Content -LiteralPath $automationLogPath -Raw } else { "(no automation log)" }
        throw "The $Scenario hotkey did not produce an observable child process. Automation: $log"
    }
    $automationExitCode = $null
    if ($null -ne $automationProcess) {
        if (-not $automationProcess.WaitForExit(15000)) {
            throw "The $Scenario UI workload automation did not finish within 15 seconds."
        }
        $automationProcess.Refresh()
        $automationExitCode = $automationProcess.ExitCode
        if ($automationExitCode -ne 0) {
            $log = if (Test-Path -LiteralPath $automationLogPath) { Get-Content -LiteralPath $automationLogPath -Raw } else { "(no automation log)" }
            $stdout = if (Test-Path -LiteralPath $automationStdoutPath) { Get-Content -LiteralPath $automationStdoutPath -Raw } else { "(no stdout)" }
            $stderr = if (Test-Path -LiteralPath $automationStderrPath) { Get-Content -LiteralPath $automationStderrPath -Raw } else { "(no stderr)" }
            throw "The $Scenario UI workload automation failed with exit code ${automationExitCode}: $log`nSTDOUT: $stdout`nSTDERR: $stderr"
        }
    }
    if ($generatedFixtures -and $Scenario -eq "EditUndoRedo") {
        foreach ($document in $launchPaths) {
            if ((Get-Item -LiteralPath $document).Length -le (8 * 1MB)) {
                $log = if (Test-Path -LiteralPath $automationLogPath) { Get-Content -LiteralPath $automationLogPath -Raw } else { "(no automation log)" }
                throw "Edit/undo/redo workload did not persist edits to $document. Automation: $log"
            }
        }
    }
    if ($generatedFixtures -and $Scenario -eq "FindReplaceUndoRedo") {
        if ((Get-Item -LiteralPath $launchPaths[0]).Length -ge (16 * 1MB)) {
            throw "Find/Replace All workload did not reduce the generated fixture size."
        }
    }
    $elapsed = [Math]::Max(0.001, ($sampleEnd - $timeStart).TotalSeconds)
    $averageSampleInterval = if ($sampleTimes.Count -gt 1) {
        ($sampleTimes[$sampleTimes.Count - 1] - $sampleTimes[0]).TotalMilliseconds / ($sampleTimes.Count - 1)
    } else { 0.0 }
    $treeCpuOneCore = if ($treeCpuSamples.Count) {
        ($treeCpuSamples[$treeCpuSamples.Count - 1] / $elapsed) * 100.0
    } else { 0.0 }
    $childCpuOneCore = if ($childCpuSamples.Count) {
        ($childCpuSamples[$childCpuSamples.Count - 1] / $elapsed) * 100.0
    } else { 0.0 }
    $cpuOneCore = [Math]::Max(0.0, $treeCpuOneCore - $childCpuOneCore)
    $averageWorkingSet = if ($workingSet.Count) { ($workingSet | Measure-Object -Average).Average / 1MB } else { 0 }
    $peakWorkingSet = if ($workingSet.Count) { ($workingSet | Measure-Object -Maximum).Maximum / 1MB } else { 0 }
    $averagePrivate = if ($privateBytes.Count) { ($privateBytes | Measure-Object -Average).Average / 1MB } else { 0 }
    $averageTreeWorkingSet = ($treeWorkingSet | Measure-Object -Average).Average / 1MB
    $peakTreeWorkingSet = ($treeWorkingSet | Measure-Object -Maximum).Maximum / 1MB
    $averageTreePrivate = ($treePrivateBytes | Measure-Object -Average).Average / 1MB
    $peakTreePrivate = ($treePrivateBytes | Measure-Object -Maximum).Maximum / 1MB
    $averageChildWorkingSet = ($childWorkingSet | Measure-Object -Average).Average / 1MB
    $peakChildWorkingSet = ($childWorkingSet | Measure-Object -Maximum).Maximum / 1MB
    $averageChildPrivate = ($childPrivateBytes | Measure-Object -Average).Average / 1MB
    $peakChildPrivate = ($childPrivateBytes | Measure-Object -Maximum).Maximum / 1MB
    $result = [pscustomobject]@{
        Executable = $executable
        ExecutableSha256 = $executableHash
        IdeSourceSetFingerprintAtMeasurementSha256 = $sourceFingerprint
        SourceFingerprintScope = "Selected IDE source files and Cargo manifests at measurement time; not embedded in executable"
        CargoVersion = $cargoVersion
        OperatingSystem = [Environment]::OSVersion.VersionString
        Scenario = $Scenario
        Document = $DocumentPath
        Documents = ($launchPaths -join ';')
        DocumentCount = $launchPaths.Count
        GeneratedFixtures = $generatedFixtures
        AutomationExitCode = $automationExitCode
        Samples = $workingSet.Count
        SampleSecondsRequested = $SampleSeconds
        SampleIntervalMilliseconds = $SampleIntervalMilliseconds
        SampleIntervalAverageMilliseconds = [Math]::Round($averageSampleInterval, 1)
        WorkingSetAverageMiB = [Math]::Round($averageWorkingSet, 1)
        WorkingSetPeakMiB = [Math]::Round($peakWorkingSet, 1)
        PrivateBytesAverageMiB = [Math]::Round($averagePrivate, 1)
        ChildWorkingSetAverageMiB = [Math]::Round($averageChildWorkingSet, 1)
        ChildWorkingSetPeakMiB = [Math]::Round($peakChildWorkingSet, 1)
        ChildPrivateBytesAverageMiB = [Math]::Round($averageChildPrivate, 1)
        ChildPrivateBytesPeakMiB = [Math]::Round($peakChildPrivate, 1)
        ProcessTreeWorkingSetAverageMiB = [Math]::Round($averageTreeWorkingSet, 1)
        ProcessTreeWorkingSetPeakMiB = [Math]::Round($peakTreeWorkingSet, 1)
        ProcessTreePrivateBytesAverageMiB = [Math]::Round($averageTreePrivate, 1)
        ProcessTreePrivateBytesPeakMiB = [Math]::Round($peakTreePrivate, 1)
        CPUOneCorePercent = [Math]::Round($cpuOneCore, 2)
        ChildCPUOneCorePercent = [Math]::Round($childCpuOneCore, 2)
        ProcessTreeCPUOneCorePercent = [Math]::Round($treeCpuOneCore, 2)
        CPUTotalMachinePercent = [Math]::Round(($cpuOneCore / $logicalProcessors), 2)
        ProcessTreeCPUTotalMachinePercent = [Math]::Round(($treeCpuOneCore / $logicalProcessors), 2)
        ProcessCountAverage = [Math]::Round(($processCounts | Measure-Object -Average).Average, 1)
        ProcessCountPeak = ($processCounts | Measure-Object -Maximum).Maximum
        LogicalProcessors = $logicalProcessors
        SampleSeconds = [Math]::Round($elapsed, 1)
    }
    $result | Format-List
    if (-not [string]::IsNullOrWhiteSpace($CsvPath)) {
        $result | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $CsvPath
    }
    if ($peakTreeWorkingSet -gt $MemoryLimitMiB) {
        throw "Observed IDE + child-process working set peak ($([Math]::Round($peakTreeWorkingSet, 1)) MiB) exceeded the $MemoryLimitMiB MiB limit."
    }
}
finally {
    if ($null -ne $automationProcess -and -not $automationProcess.HasExited) {
        Stop-Process -Id $automationProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    if ($null -ne $fixtureDirectory -and (Test-Path -LiteralPath $fixtureDirectory)) {
        if ($env:RBL_STUDIO_KEEP_FIXTURES -eq "1") {
            Write-Output "FixtureDirectoryPreserved=$fixtureDirectory"
        } else {
            Remove-Item -LiteralPath $fixtureDirectory -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
