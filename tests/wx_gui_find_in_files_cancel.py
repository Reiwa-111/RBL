import os,pathlib,subprocess,time,ctypes,tempfile,shutil,psutil
import win32con,win32gui,win32process,win32api,win32clipboard
root=pathlib.Path.cwd();exe=root/'build/wx-ui-win/rbl-studio-wx.exe';worker=root/'build/wx-ui-win/rbl-studio-worker.exe'
fixture=root/'gui-search-cancel-acceptance';fixture.mkdir(exist_ok=True)
payload=b'RBL_SEARCH_CANCEL_TEST '+b'x'*(256*1024-24)
for i in range(512):(fixture/f'file-{i:04d}.txt').write_bytes(payload)
profile=pathlib.Path(tempfile.mkdtemp());(profile/'ui.ini').write_text('[Workspace]\nRoot='+fixture.as_posix()+'\n')
env=os.environ.copy();env['RBL_STUDIO_CONFIG_DIR']=str(profile);env['RBL_STUDIO_WORKER']=str(worker);env['RBL_PYTHON']=__import__('sys').executable
def frame_for(pid):
 a=[];win32gui.EnumWindows(lambda h,_:a.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='RBL Studio' else None,None);return a[0]
def dialog_for(pid):
 a=[];win32gui.EnumWindows(lambda h,_:a.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='Find in Files' else None,None);return a[0] if a else 0
def enter_search(pid,query):
 dlg=dialog_for(pid)
 edits=[];win32gui.EnumChildWindows(dlg,lambda h,_:edits.append(h) if win32gui.GetClassName(h)=='Edit' else None,None)
 if not edits:return False
 tid=win32process.GetWindowThreadProcessId(dlg)[0];cur=ctypes.windll.kernel32.GetCurrentThreadId();ctypes.windll.user32.AttachThreadInput(cur,tid,True)
 try:
  win32api.keybd_event(win32con.VK_MENU,0,0,0);time.sleep(.03)
  try:win32gui.SetForegroundWindow(dlg)
  except Exception:pass
  win32api.keybd_event(win32con.VK_MENU,0,win32con.KEYEVENTF_KEYUP,0);win32gui.SetFocus(edits[0])
 finally:ctypes.windll.user32.AttachThreadInput(cur,tid,False)
 win32clipboard.OpenClipboard()
 try:win32clipboard.EmptyClipboard();win32clipboard.SetClipboardData(win32con.CF_UNICODETEXT,query)
 finally:win32clipboard.CloseClipboard()
 def key(vk,flags=0):win32api.keybd_event(vk,0,flags,0)
 def chord(vk):key(win32con.VK_CONTROL);key(vk);time.sleep(.04);key(vk,win32con.KEYEVENTF_KEYUP);key(win32con.VK_CONTROL,win32con.KEYEVENTF_KEYUP)
 chord(ord('A'));chord(ord('V'));time.sleep(.1);key(win32con.VK_RETURN);key(win32con.VK_RETURN,win32con.KEYEVENTF_KEYUP)
 for _ in range(50):
  time.sleep(.05)
  if not win32gui.IsWindow(dlg):return True
 return False
p=subprocess.Popen([str(exe)],cwd=root,env=env)
try:
 time.sleep(4);frame=frame_for(p.pid)
 win32gui.PostMessage(frame,win32con.WM_COMMAND,6025,0)
 for _ in range(50):
  time.sleep(.1)
  if dialog_for(p.pid):break
 if not enter_search(p.pid,'unlikely_search_token'):raise RuntimeError('initial search did not start')
 time.sleep(.03);win32gui.PostMessage(frame,win32con.WM_COMMAND,6034,0)
 time.sleep(3)
 win32gui.PostMessage(frame,win32con.WM_COMMAND,6025,0)
 dlg=0;has_edit=False
 for _ in range(100):
  time.sleep(.1);dlg=dialog_for(p.pid)
  if dlg:
   edits=[];win32gui.EnumChildWindows(dlg,lambda h,_:edits.append(h) if win32gui.GetClassName(h)=='Edit' else None,None)
   if edits:has_edit=True;break
   win32gui.PostMessage(dlg,win32con.WM_COMMAND,5100,0)
 if not has_edit:raise RuntimeError('search remained active after Cancel Search')
 win32gui.PostMessage(dlg,win32con.WM_KEYDOWN,win32con.VK_ESCAPE,0)
 win32gui.PostMessage(dlg,win32con.WM_KEYUP,win32con.VK_ESCAPE,0)
 time.sleep(.3)
 if not win32gui.IsWindow(frame):raise RuntimeError('IDE unexpectedly closed after search cancellation')
 print('Cancel Find in Files + re-entry: PASS',flush=True)
finally:
 subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);shutil.rmtree(fixture,ignore_errors=True);shutil.rmtree(profile,ignore_errors=True)
