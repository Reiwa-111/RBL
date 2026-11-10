import os,pathlib,subprocess,time,ctypes,tempfile,shutil,psutil
import win32con,win32gui,win32process,win32api,win32clipboard
root=pathlib.Path.cwd();exe=root/'build/wx-ui-win/rbl-studio-wx.exe';worker=root/'build/wx-ui-win/rbl-studio-worker.exe'
fixture=root/'gui-search-cancel-acceptance';fixture.mkdir(exist_ok=True)
payload=b'RBL_SEARCH_CANCEL_TEST '+b'x'* (256*1024-24)
for i in range(512): (fixture/f'file-{i:04d}.txt').write_bytes(payload)
profile=pathlib.Path(tempfile.mkdtemp(prefix='rblstudio-search-cancel-'));(profile/'ui.ini').write_text('[Workspace]\nRoot='+fixture.as_posix()+'\n')
env=os.environ.copy();env['RBL_STUDIO_CONFIG_DIR']=str(profile);env['RBL_STUDIO_WORKER']=str(worker);env['RBL_PYTHON']=__import__('sys').executable
def frame_for(pid):
 out=[];win32gui.EnumWindows(lambda h,_:out.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='RBL Studio' else None,None)
 if not out:raise RuntimeError('main frame missing')
 return out[0]
def start_search(pid):
 frame=frame_for(pid);win32gui.PostMessage(frame,win32con.WM_COMMAND,6025,0);dlg=0
 for _ in range(50):
  time.sleep(.1);ds=[];win32gui.EnumWindows(lambda h,_:ds.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='Find in Files' else None,None)
  if ds:dlg=ds[0];break
 if not dlg:raise RuntimeError('search dialog missing')
 edits=[];win32gui.EnumChildWindows(dlg,lambda h,_:edits.append(h) if win32gui.GetClassName(h)=='Edit' else None,None)
 tid=win32process.GetWindowThreadProcessId(dlg)[0];cur=ctypes.windll.kernel32.GetCurrentThreadId();ctypes.windll.user32.AttachThreadInput(cur,tid,True)
 try:
  win32api.keybd_event(win32con.VK_MENU,0,0,0);time.sleep(.03)
  try:win32gui.SetForegroundWindow(dlg)
  except Exception:pass
  win32api.keybd_event(win32con.VK_MENU,0,win32con.KEYEVENTF_KEYUP,0);win32gui.SetFocus(edits[0])
 finally:ctypes.windll.user32.AttachThreadInput(cur,tid,False)
 win32clipboard.OpenClipboard()
 try:win32clipboard.EmptyClipboard();win32clipboard.SetClipboardData(win32con.CF_UNICODETEXT,'unlikely_search_token')
 finally:win32clipboard.CloseClipboard()
 def key(vk,flags=0):win32api.keybd_event(vk,0,flags,0)
 def chord(vk):key(win32con.VK_CONTROL);key(vk);time.sleep(.04);key(vk,win32con.KEYEVENTF_KEYUP);key(win32con.VK_CONTROL,win32con.KEYEVENTF_KEYUP)
 chord(ord('A'));chord(ord('V'));time.sleep(.1);key(win32con.VK_RETURN);key(win32con.VK_RETURN,win32con.KEYEVENTF_KEYUP)
 for _ in range(50):
  time.sleep(.05)
  if not win32gui.IsWindow(dlg):return frame
 raise RuntimeError('search dialog did not close')
p=subprocess.Popen([str(exe)],cwd=root,env=env)
try:
 time.sleep(4);frame=start_search(p.pid)
 time.sleep(.03)
 children=psutil.Process(p.pid).children(recursive=True)
 win32gui.PostMessage(frame,win32con.WM_CLOSE,0,0)
 deadline=time.monotonic()+60
 while p.poll() is None and time.monotonic()<deadline:
  time.sleep(1)
 if p.poll() is None:
  windows=[]
  win32gui.EnumWindows(lambda h,_:windows.append((win32gui.GetClassName(h),win32gui.GetWindowText(h),win32gui.IsWindowVisible(h))) if win32process.GetWindowThreadProcessId(h)[1]==p.pid else None,None)
  try:cpu=psutil.Process(p.pid).cpu_times().user+psutil.Process(p.pid).cpu_times().system
  except psutil.Error:cpu=-1
  raise RuntimeError(f'IDE did not close after cancelling active Find in Files; windows={windows}, cpu={cpu:.2f}s')
 for child in children:
  try:child.wait(timeout=3)
  except psutil.TimeoutExpired:
   child.terminate()
   try:child.wait(timeout=2)
   except psutil.TimeoutExpired:child.kill()
 print('close-during-Find-in-Files: PASS (deferred close completed)',flush=True)
finally:
 if p.poll() is None:subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 shutil.rmtree(fixture,ignore_errors=True);shutil.rmtree(profile,ignore_errors=True)
