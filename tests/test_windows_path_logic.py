from pathlib import PureWindowsPath

def posix_style_windows_path(value: str) -> str:
    return PureWindowsPath(value).as_posix()

assert posix_style_windows_path(r"Q:\ReiwaBatLanguage\RBLStudio\compiler\rblc_asm.c") == "Q:/ReiwaBatLanguage/RBLStudio/compiler/rblc_asm.c"
assert posix_style_windows_path(r"C:\Program Files\RBL Studio\main.rbl") == "C:/Program Files/RBL Studio/main.rbl"
print("Windows path normalization: PASS")
