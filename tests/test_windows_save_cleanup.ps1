$ErrorActionPreference = 'Stop'

$native = @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class RblWindowsSaveCleanupTestApi
{
    [StructLayout(LayoutKind.Sequential)]
    public struct FileDispositionInfo
    {
        [MarshalAs(UnmanagedType.Bool)] public bool DeleteFile;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool CreateHardLink(string newName, string existingName, IntPtr securityAttributes);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern SafeFileHandle CreateFile(
        string name, uint access, uint share, IntPtr securityAttributes,
        uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetFileInformationByHandle(
        SafeFileHandle file, int informationClass,
        ref FileDispositionInfo information, uint size);

    public static SafeFileHandle OpenForDelete(string path)
    {
        // DELETE | FILE_READ_ATTRIBUTES, all share modes, OPEN_EXISTING,
        // FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT.
        var handle = CreateFile(path, 0x00010000u | 0x80u, 1u | 2u | 4u,
            IntPtr.Zero, 3u, 0x80u | 0x00200000u, IntPtr.Zero);
        if (handle.IsInvalid)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateFile for delete failed");
        return handle;
    }

    public static void MarkOpenedFileForDelete(SafeFileHandle handle)
    {
        var disposition = new FileDispositionInfo { DeleteFile = true };
        // FileDispositionInfo is FILE_INFO_BY_HANDLE_CLASS value 4.
        if (!SetFileInformationByHandle(handle, 4, ref disposition,
                (uint)Marshal.SizeOf(typeof(FileDispositionInfo))))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "SetFileInformationByHandle failed");
    }
}
'@

Add-Type -TypeDefinition $native
$root = Join-Path $env:TEMP ('RBL Save Cleanup ' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    # Deleting one hard-link name by handle must leave the original name alive.
    $original = Join-Path $root 'original.txt'
    $backup = Join-Path $root 'backup.link'
    [System.IO.File]::WriteAllText($original, 'original bytes')
    if (-not [RblWindowsSaveCleanupTestApi]::CreateHardLink($backup, $original, [IntPtr]::Zero)) {
        throw 'CreateHardLink failed for the basic cleanup test.'
    }
    $handle = [RblWindowsSaveCleanupTestApi]::OpenForDelete($backup)
    try { [RblWindowsSaveCleanupTestApi]::MarkOpenedFileForDelete($handle) }
    finally { $handle.Dispose() }
    if (-not (Test-Path -LiteralPath $original) -or (Test-Path -LiteralPath $backup)) {
        throw 'Handle-based cleanup did not remove exactly the backup hard-link name.'
    }

    # If the pathname is renamed/replaced after opening, disposition applies to
    # the already-opened file object, not to the replacement at the old name.
    $original2 = Join-Path $root 'original-race.txt'
    $link2 = Join-Path $root 'recovery.link'
    $moved2 = Join-Path $root 'recovery-moved.link'
    [System.IO.File]::WriteAllText($original2, 'preserve original')
    if (-not [RblWindowsSaveCleanupTestApi]::CreateHardLink($link2, $original2, [IntPtr]::Zero)) {
        throw 'CreateHardLink failed for the pathname replacement test.'
    }
    $handle2 = [RblWindowsSaveCleanupTestApi]::OpenForDelete($link2)
    try {
        Move-Item -LiteralPath $link2 -Destination $moved2
        [System.IO.File]::WriteAllText($link2, 'new unrelated replacement')
        [RblWindowsSaveCleanupTestApi]::MarkOpenedFileForDelete($handle2)
    } finally {
        $handle2.Dispose()
    }
    if (-not (Test-Path -LiteralPath $original2)) {
        throw 'Handle-based cleanup removed the original file instead of one recovery link.'
    }
    if (Test-Path -LiteralPath $moved2) {
        throw 'Handle-based cleanup failed to remove the opened recovery link after its rename.'
    }
    if (-not (Test-Path -LiteralPath $link2) -or
        (Get-Content -LiteralPath $link2 -Raw) -cne 'new unrelated replacement') {
        throw 'Handle-based cleanup deleted or modified the replacement at the old pathname.'
    }

    Write-Output 'Windows Save handle-based hard-link cleanup tests passed.'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
