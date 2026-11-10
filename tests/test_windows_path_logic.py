from pathlib import PureWindowsPath

def posix_style_windows_path(value: str) -> str:
    return PureWindowsPath(value).as_posix()

assert posix_style_windows_path(r"Q:\ReiwaBatLanguage\RBLStudio\compiler\driver.c") == "Q:/ReiwaBatLanguage/RBLStudio/compiler/driver.c"
assert posix_style_windows_path(r"C:\Program Files\RBL Studio\main.rbl") == "C:/Program Files/RBL Studio/main.rbl"
print("Windows path normalization: PASS")
