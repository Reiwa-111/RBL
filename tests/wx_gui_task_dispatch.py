import os,pathlib,subprocess,time,tempfile,shutil,ctypes,psutil
import win32gui,win32con,win32process
root=pathlib.Path.cwd();exe=root/'build/wx-ui-win/rbl-studio-wx.exe';worker=root/'build/wx-ui-win/rbl-studio-worker.exe'
fixture=root/'build/wx-ui-win/gui-acceptance';fixture.mkdir(exist_ok=True)
src=fixture/"gui-task-smoke.rbl";src.write_text("func main() ("+chr(10)+"    print("+chr(34)+"GUI_TASK_OK"+chr(34)+")"+chr(10)+")"+chr(10),encoding="utf-8")
profile=pathlib.Path(tempfile.mkdtemp());env=os.environ.copy();env['RBL_STUDIO_CONFIG_DIR']=str(profile);env['RBL_STUDIO_WORKER']=str(worker);env['RBL_PYTHON']=__import__('sys').executable
u=ctypes.windll.user32;k=ctypes.windll.kernel32
k.OpenProcess.argtypes=[ctypes.c_uint,ctypes.c_int,ctypes.c_uint];k.OpenProcess.restype=ctypes.c_void_p
k.VirtualAllocEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint,ctypes.c_uint];k.VirtualAllocEx.restype=ctypes.c_void_p
k.ReadProcessMemory.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.ReadProcessMemory.restype=ctypes.c_int
k.VirtualFreeEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint];k.CloseHandle.argtypes=[ctypes.c_void_p]
u.SendMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t];u.SendMessageW.restype=ctypes.c_ssize_t
def status(pid,frame):
 bars=[];win32gui.EnumChildWindows(frame,lambda h,_:bars.append(h) if win32gui.GetClassName(h)=='msctls_statusbar32' else None,None)
 hp=k.OpenProcess(0x438,False,pid);addr=k.VirtualAllocEx(hp,None,512,0x3000,4);out=[];n=ctypes.c_size_t()
 try:
  for i in range(4):
   u.SendMessageW(bars[0],0x040D,i,addr);buf=ctypes.create_string_buffer(512);k.ReadProcessMemory(hp,addr,buf,512,ctypes.byref(n));out.append(buf.raw.decode('utf-16le',errors='replace').split('\0')[0])
 finally:k.VirtualFreeEx(hp,addr,0,0x8000);k.CloseHandle(hp)
 return out
p=subprocess.Popen([str(exe),str(src)],cwd=root,env=env)
try:
 time.sleep(4);frames=[];win32gui.EnumWindows(lambda h,_:frames.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==p.pid and win32gui.GetWindowText(h)=='RBL Studio' else None,None)
 if not frames:raise RuntimeError('main frame missing')
 frame=frames[0]
 before=status(p.pid,frame);print('START',before,flush=True)
 commands=[('Check',6006),('Build',6007),('Run',6005),('Tests',6020)]
 for name,command in commands:
  prior=status(p.pid,frame)[2]
  win32gui.PostMessage(frame,win32con.WM_COMMAND,command,0)
  deadline=time.monotonic()+300
  saw_transition=False;final=None
  while time.monotonic()<deadline:
   time.sleep(.05);current=status(p.pid,frame)[2]
   if current!=prior:saw_transition=True
   if saw_transition and (current=='Finished' or current.startswith('Exit code ')):
    final=current;break
  print(name,'status=',final,'transition_seen=',saw_transition,flush=True)
  if final!='Finished' and final!='Exit code 0':raise RuntimeError(f'{name} task failed or timed out: {final}')
 print('GUI task dispatch Check/Build/Run/Tests: 4/4 PASS',flush=True)
finally:
 subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);shutil.rmtree(profile,ignore_errors=True)
