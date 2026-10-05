' RBL Studio GUI launcher: starts Python without opening a console window.
Option Explicit
Dim shell, fso, root, py
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
py = root & "\.venv\Scripts\pythonw.exe"
If Not fso.FileExists(py) Then py = "pythonw.exe"
shell.Run """" & py & """ """ & root & "\RBLStudio.py"""", 0, False
