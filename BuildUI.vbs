' ===========================================================================
' BuildUI.vbs - launch the checkbox build tool with no console window at all.
'
' A .bat always opens a console window for a moment, so this is the
' double-click entry point: wscript is a GUI host and starts the tool through
' the windowless Python launcher. BuildUI.bat forwards here as well.
'
' Keep this file ASCII-only: Windows Script Host reads .vbs as ANSI unless it
' has a BOM, so non-ASCII text turns into a compile error on CJK systems.
' ===========================================================================
Option Explicit

Dim fso, sh, toolDir, scriptPath, pythonExe

Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

toolDir = fso.GetParentFolderName(WScript.ScriptFullName)
scriptPath = fso.BuildPath(toolDir, "BuildUI.py")

If Not fso.FileExists(scriptPath) Then
    MsgBox "BuildUI.py was not found:" & vbCrLf & scriptPath, 16, "Build Tool"
    WScript.Quit 1
End If

pythonExe = FindOnPath("pyw.exe")
If pythonExe = "" Then pythonExe = FindOnPath("pythonw.exe")
If pythonExe = "" Then pythonExe = FindOnPath("python.exe")

If pythonExe = "" Then
    MsgBox "No pyw.exe / pythonw.exe / python.exe found." & vbCrLf & _
           "Install Python and make sure it is on PATH.", 16, "Build Tool"
    WScript.Quit 1
End If

sh.CurrentDirectory = toolDir
sh.Run """" & pythonExe & """ """ & scriptPath & """", 0, False

' ---------------------------------------------------------------------------
' FindOnPath - first existing hit for a file name in %PATH%
' ---------------------------------------------------------------------------
Function FindOnPath(fileName)
    Dim pathVar, parts, i, candidate
    FindOnPath = ""
    pathVar = sh.ExpandEnvironmentStrings("%PATH%")
    parts = Split(pathVar, ";")
    For i = 0 To UBound(parts)
        candidate = Trim(parts(i))
        If Len(candidate) > 0 Then
            candidate = fso.BuildPath(candidate, fileName)
            If fso.FileExists(candidate) Then
                FindOnPath = candidate
                Exit Function
            End If
        End If
    Next
End Function
