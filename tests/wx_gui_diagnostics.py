import os, pathlib, subprocess, time, ctypes
import win32con, win32gui, win32process
root=pathlib.Path.cwd()
exe=pathlib.Path(os.environ.get('RBL_STUDIO_GUI',root/'build/wx-ui-win/rbl-studio-wx.exe'))
worker=pathlib.Path(os.environ.get('RBL_STUDIO_WORKER',root/'build/wx-ui-win/rbl-studio-worker.exe'))
env=os.environ.copy(); env['RBL_STUDIO_WORKER']=str(worker); env['RBL_PYTHON']=__import__('sys').executable
fixture=root/'build/wx-ui-win/gui-acceptance'; fixture.mkdir(parents=True,exist_ok=True)
source='func main() (\n    print("before")\n    break\n)\n'
u=ctypes.windll.user32; k=ctypes.windll.kernel32
u.SendMessageA.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t]; u.SendMessageA.restype=ctypes.c_ssize_t
k.OpenProcess.argtypes=[ctypes.c_uint,ctypes.c_int,ctypes.c_uint]; k.OpenProcess.restype=ctypes.c_void_p
k.VirtualAllocEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint,ctypes.c_uint]; k.VirtualAllocEx.restype=ctypes.c_void_p
k.WriteProcessMemory.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]; k.WriteProcessMemory.restype=ctypes.c_int
k.VirtualFreeEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint]; k.CloseHandle.argtypes=[ctypes.c_void_p]
def frame_for(pid):
    found=[]
    win32gui.EnumWindows(lambda h,_: found.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='RBL Studio' else None,None)
    if not found: raise RuntimeError('main frame not found')
    return found[0]
def children(frame, cls):
    out=[]; win32gui.EnumChildWindows(frame,lambda h,_:out.append(h) if win32gui.GetClassName(h)==cls else None,None); return out
def set_text(pid, hwnd, data):
    hp=k.OpenProcess(0x0438,False,pid)
    if not hp: raise RuntimeError('OpenProcess failed')
    remote=None
    try:
        buf=ctypes.create_string_buffer(data); remote=k.VirtualAllocEx(hp,None,len(data),0x3000,0x04)
        if not remote: raise RuntimeError('VirtualAllocEx failed')
        n=ctypes.c_size_t()
        if not k.WriteProcessMemory(hp,remote,buf,len(data),ctypes.byref(n)) or n.value!=len(data): raise RuntimeError('WriteProcessMemory failed')
        u.SendMessageA(hwnd,2181,0,remote)
    finally:
        if remote: k.VirtualFreeEx(hp,remote,0,0x8000)
        k.CloseHandle(hp)
def run_case(name, saved):
    path=fixture/f'diagnostic-{name}.rbl'
    if saved: path.write_text(source,encoding='utf-8')
    p=subprocess.Popen([str(exe),str(path)] if saved else [str(exe)],cwd=root,env=env)
    try:
        time.sleep(4)
        frame=frame_for(p.pid)
        editors=children(frame,'Scintilla')
        lists=[h for h in children(frame,'SysListView32') if win32gui.GetDlgCtrlID(h)==6023]
        if not editors or not lists: raise RuntimeError('editor/problems controls missing')
        editor=editors[-1]; problems=lists[0]
        if not saved: set_text(p.pid,editor,source.encode('utf-8'))
        win32gui.PostMessage(frame,win32con.WM_COMMAND,6006,0)
        count=0
        for _ in range(150):
            time.sleep(.1); count=u.SendMessageW(problems,0x1004,0,0)
            if count: break
        print(name,'problem_count=',count,flush=True)
        if count!=1: raise RuntimeError(f'expected exactly one diagnostic, got {count}')
        cols=[u.SendMessageW(problems,0x102D,0,0)]
        # Activate first row with an actual list-control double-click message.
        win32gui.PostMessage(problems,win32con.WM_LBUTTONDOWN,win32con.MK_LBUTTON,(10<<16)|30)
        win32gui.PostMessage(problems,win32con.WM_LBUTTONUP,0,(10<<16)|30)
        win32gui.PostMessage(problems,win32con.WM_LBUTTONDBLCLK,win32con.MK_LBUTTON,(10<<16)|30)
        time.sleep(.7)
        pos=u.SendMessageW(editor,2143,0,0)  # SCI_GETSELECTIONSTART
        end=u.SendMessageW(editor,2145,0,0)  # SCI_GETSELECTIONEND
        line=u.SendMessageW(editor,2166,pos,0)
        col=u.SendMessageW(editor,2129,pos,0)
        char=u.SendMessageW(editor,2007,pos,0)
        print(name,'selection=',(pos,end),'line0=',line,'column0=',col,'char=',char,flush=True)
        if line!=2 or col!=4 or char!=ord('b') or end!=pos+1:
            raise RuntimeError(f'diagnostic selection wrong: line={line}, col={col}, char={char}, end={end}')
        if not saved:
            win32gui.PostMessage(frame,win32con.WM_COMMAND,6017,0)
            dialog=0
            for _ in range(50):
                time.sleep(.1)
                ds=[]
                win32gui.EnumWindows(lambda h,_: ds.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==p.pid and win32gui.GetWindowText(h)=='Unsaved changes' else None,None)
                if ds: dialog=ds[0]; break
            if not dialog: raise RuntimeError('unsaved-close confirmation did not appear')
            win32gui.PostMessage(dialog,win32con.WM_COMMAND,7,0)  # discard
            time.sleep(.3)
            win32gui.PostMessage(problems,win32con.WM_LBUTTONDOWN,win32con.MK_LBUTTON,(10<<16)|30)
            win32gui.PostMessage(problems,win32con.WM_LBUTTONUP,0,(10<<16)|30)
            win32gui.PostMessage(problems,win32con.WM_LBUTTONDBLCLK,win32con.MK_LBUTTON,(10<<16)|30)
            time.sleep(.3)
            if p.poll() is not None or not win32gui.IsWindow(frame):
                raise RuntimeError('stale diagnostic activation crashed/closed the IDE')
            print(name,'closed-tab stale diagnostic safely ignored',flush=True)
        print(name,'PASS',flush=True)
    finally:
        if p.poll() is None: subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
run_case('saved',True)
run_case('unsaved',False)
print('diagnostic navigation: 2/2 PASS')

