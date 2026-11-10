' Hidden-window wxWidgets launcher. Arguments are passed directly to the GUI.
Option Explicit

Dim shell, fso, root, gui, worker, buildDir, pointerPath, stream, allowLegacy, args, i
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
shell.CurrentDirectory = root
gui = ""
worker = ""

If fso.FileExists(root & "\rbl-studio-wx.exe") And fso.FileExists(root & "\rbl-studio-worker.exe") Then
    gui = root & "\rbl-studio-wx.exe"
    worker = root & "\rbl-studio-worker.exe"
Else
    pointerPath = root & "\build\wx-ui-current-win.path"
    If fso.FileExists(pointerPath) Then
        Set stream = fso.OpenTextFile(pointerPath, 1, False)
        If Not stream.AtEndOfStream Then buildDir = Trim(stream.ReadLine)
        stream.Close
        If Len(buildDir) > 0 Then
            If Not (Mid(buildDir, 2, 1) = ":" Or Left(buildDir, 2) = "\\") Then
                buildDir = root & "\" & buildDir
            End If
            buildDir = fso.GetAbsolutePathName(buildDir)
            If fso.FileExists(buildDir & "\rbl-studio-wx.exe") And fso.FileExists(buildDir & "\rbl-studio-worker.exe") Then
                gui = buildDir & "\rbl-studio-wx.exe"
                worker = buildDir & "\rbl-studio-worker.exe"
            End If
        End If
    End If
End If

allowLegacy = shell.ExpandEnvironmentStrings("%RBL_STUDIO_ALLOW_LEGACY%")
If Len(gui) = 0 And allowLegacy = "1" Then
    If fso.FileExists(root & "\target\release\rbl-studio.exe") Then
        gui = root & "\target\release\rbl-studio.exe"
    ElseIf fso.FileExists(root & "\target\x86_64-pc-windows-gnu\release\rbl-studio.exe") Then
        gui = root & "\target\x86_64-pc-windows-gnu\release\rbl-studio.exe"
    End If
    If Len(gui) = 0 Then WScript.Quit 1
ElseIf Len(gui) = 0 Then
    WScript.Echo "RBL Studio wxWidgets build was not found."
    WScript.Quit 1
End If

If allowLegacy <> "1" Or InStr(1, gui, "rbl-studio-wx.exe", vbTextCompare) > 0 Then
    shell.Environment("PROCESS")("RBL_STUDIO_ROOT") = root
    shell.Environment("PROCESS")("RBL_STUDIO_WORKER") = worker
End If

args = QuoteWindowsArgument(gui)
For i = 0 To WScript.Arguments.Count - 1
    args = args & " " & QuoteWindowsArgument(WScript.Arguments(i))
Next
shell.Run args, 0, False

Function QuoteWindowsArgument(value)
    Dim result, slashCount, ch, j
    result = Chr(34)
    slashCount = 0
    For j = 1 To Len(value)
        ch = Mid(value, j, 1)
        If ch = Chr(92) Then
            slashCount = slashCount + 1
        ElseIf ch = Chr(34) Then
            result = result & String((slashCount * 2) + 1, Chr(92)) & Chr(34)
            slashCount = 0
        Else
            If slashCount > 0 Then result = result & String(slashCount, Chr(92))
            result = result & ch
            slashCount = 0
        End If
    Next
    If slashCount > 0 Then result = result & String(slashCount * 2, Chr(92))
    QuoteWindowsArgument = result & Chr(34)
End Function
