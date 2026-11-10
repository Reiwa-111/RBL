import os,pathlib,subprocess,time,ctypes,tempfile,shutil
import win32con,win32gui,win32process,win32api,win32clipboard
root=pathlib.Path.cwd(); exe=pathlib.Path(os.environ.get('RBL_STUDIO_GUI',root/'build/wx-ui-win/rbl-studio-wx.exe')); worker=pathlib.Path(os.environ.get('RBL_STUDIO_WORKER',root/'build/wx-ui-win/rbl-studio-worker.exe'))
fixture=pathlib.Path(tempfile.mkdtemp(prefix='wx-find-acceptance-',dir=root/'build/wx-ui-win'))
tests={'cr-only':('first\rneedle_cr_only\rthird\r','needle_cr_only'),'crlf':('first\r\nneedle_crlf\r\nthird\r\n','needle_crlf'),'lf':('first\nneedle_lf\nthird\n','needle_lf')}
for name,(content,_) in tests.items(): (fixture/f'{name}.txt').write_bytes(content.encode())
readable_dir=fixture/'00-readable'; readable_dir.mkdir(exist_ok=True)
readable_hit=readable_dir/'found.txt'; readable_hit.write_text('needle_after_denied\n',encoding='utf-8')
denied_dir=fixture/'zz-denied'; denied_dir.mkdir(exist_ok=True)
(denied_dir/'hidden.txt').write_text('needle_hidden\n',encoding='utf-8')
acl=subprocess.run(['icacls',str(denied_dir),'/deny','*S-1-1-0:(OI)(CI)(RX)'],capture_output=True,text=True)
permission_denied=False
if acl.returncode==0:
 try: list(denied_dir.iterdir())
 except PermissionError: permission_denied=True
symlink_path=fixture/'link-to-root'
symlink_created=False
junction_created=False
external_dir=fixture.parent/f'wx-find-external-{os.getpid()}'
external_file=external_dir/'outside.txt'
external_junction=fixture/'external-junction'
external_junction_created=False
external_file_link=fixture/'external-file-link.txt'
external_file_link_created=False
profile=pathlib.Path(tempfile.mkdtemp(prefix='rblstudio-search-profile-'))
(profile/'ui.ini').write_text('[Workspace]\nRoot='+fixture.as_posix()+'\n',encoding='utf-8')
env=os.environ.copy(); env['RBL_STUDIO_CONFIG_DIR']=str(profile); env['RBL_STUDIO_WORKER']=str(worker); env['RBL_PYTHON']=__import__('sys').executable
u=ctypes.windll.user32;k=ctypes.windll.kernel32
k.OpenProcess.argtypes=[ctypes.c_uint,ctypes.c_int,ctypes.c_uint];k.OpenProcess.restype=ctypes.c_void_p
k.VirtualAllocEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint,ctypes.c_uint];k.VirtualAllocEx.restype=ctypes.c_void_p
k.WriteProcessMemory.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.WriteProcessMemory.restype=ctypes.c_int
k.ReadProcessMemory.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)];k.ReadProcessMemory.restype=ctypes.c_int
k.VirtualFreeEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint];k.CloseHandle.argtypes=[ctypes.c_void_p]
u.SendMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t];u.SendMessageW.restype=ctypes.c_ssize_t
class LVITEM(ctypes.Structure):
 _fields_=[('mask',ctypes.c_uint),('iItem',ctypes.c_int),('iSubItem',ctypes.c_int),('state',ctypes.c_uint),('stateMask',ctypes.c_uint),('pszText',ctypes.c_void_p),('cchTextMax',ctypes.c_int),('iImage',ctypes.c_int),('lParam',ctypes.c_ssize_t),('iIndent',ctypes.c_int),('iGroupId',ctypes.c_int),('cColumns',ctypes.c_uint),('puColumns',ctypes.c_void_p),('piColFmt',ctypes.c_void_p),('iGroup',ctypes.c_int)]
def frame_for(pid):
 out=[];win32gui.EnumWindows(lambda h,_:out.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='RBL Studio' else None,None)
 if not out: raise RuntimeError('main frame missing')
 return out[0]
def list_text(pid,hwnd,row,col):
 hp=k.OpenProcess(0x0438,False,pid);remote_text=remote_item=None
 try:
  remote_text=k.VirtualAllocEx(hp,None,1024,0x3000,0x04);remote_item=k.VirtualAllocEx(hp,None,ctypes.sizeof(LVITEM),0x3000,0x04)
  item=LVITEM();item.iItem=row;item.iSubItem=col;item.pszText=remote_text;item.cchTextMax=256
  local=ctypes.create_string_buffer(bytes(item));n=ctypes.c_size_t()
  if not k.WriteProcessMemory(hp,remote_item,local,len(local),ctypes.byref(n)):raise RuntimeError('WriteProcessMemory LVITEM failed')
  u.SendMessageW(hwnd,0x1073,row,remote_item)
  data=ctypes.create_string_buffer(1024);k.ReadProcessMemory(hp,remote_text,data,1024,ctypes.byref(n))
  return data.raw.decode('utf-16le',errors='replace').split('\0')[0]
 finally:
  if remote_item:k.VirtualFreeEx(hp,remote_item,0,0x8000)
  if remote_text:k.VirtualFreeEx(hp,remote_text,0,0x8000)
  k.CloseHandle(hp)
def submit_search(pid,query):
 frame=frame_for(pid);win32gui.PostMessage(frame,win32con.WM_COMMAND,6025,0)
 dlg=0
 for _ in range(50):
  time.sleep(.1);ds=[]
  win32gui.EnumWindows(lambda h,_:ds.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='Find in Files' else None,None)
  if ds:dlg=ds[0];break
 if not dlg:raise RuntimeError('Find in Files dialog did not appear')
 edits=[];win32gui.EnumChildWindows(dlg,lambda h,_:edits.append(h) if win32gui.GetClassName(h)=='Edit' else None,None)
 tid=win32process.GetWindowThreadProcessId(dlg)[0];cur=k.GetCurrentThreadId();u.AttachThreadInput(cur,tid,True)
 try:
  win32api.keybd_event(win32con.VK_MENU,0,0,0);time.sleep(.03)
  try:win32gui.SetForegroundWindow(dlg)
  except Exception:pass
  win32api.keybd_event(win32con.VK_MENU,0,win32con.KEYEVENTF_KEYUP,0);win32gui.SetFocus(edits[0])
 finally:u.AttachThreadInput(cur,tid,False)
 win32clipboard.OpenClipboard()
 try:win32clipboard.EmptyClipboard();win32clipboard.SetClipboardData(win32con.CF_UNICODETEXT,query)
 finally:win32clipboard.CloseClipboard()
 def key(vk,flags=0):win32api.keybd_event(vk,0,flags,0)
 def chord(vk):key(win32con.VK_CONTROL);key(vk);time.sleep(.04);key(vk,win32con.KEYEVENTF_KEYUP);key(win32con.VK_CONTROL,win32con.KEYEVENTF_KEYUP)
 chord(ord('A'));chord(ord('V'));time.sleep(.15);key(win32con.VK_RETURN);key(win32con.VK_RETURN,win32con.KEYEVENTF_KEYUP)
 for _ in range(50):
  time.sleep(.1)
  if not win32gui.IsWindow(dlg):return
 raise RuntimeError('Find in Files dialog did not close after Enter')
p=subprocess.Popen([str(exe)],cwd=root,env=env)
try:
 time.sleep(4);frame=frame_for(p.pid)
 lists=[];win32gui.EnumChildWindows(frame,lambda h,_:lists.append(h) if win32gui.GetClassName(h)=='SysListView32' and win32gui.GetDlgCtrlID(h)==6026 else None,None)
 if not lists:raise RuntimeError('Search Results list not found')
 results=lists[0]
 for name,(_,query) in tests.items():
  submit_search(p.pid,query)
  count=0
  for _ in range(100):
   time.sleep(.1);count=u.SendMessageW(results,0x1004,0,0)
   if count:break
  print(name,'count=',count,flush=True)
  if not count:
   debug=[]
   win32gui.EnumChildWindows(frame_for(p.pid),lambda h,_:debug.append(
    (win32gui.GetDlgCtrlID(h),win32gui.GetClassName(h),win32gui.GetWindowText(h)[:240])),None)
   print('Find in Files zero-result child diagnostics:',debug,flush=True)
  if count!=1:raise RuntimeError(f'{name}: expected one hit, got {count}')
  path_text=list_text(p.pid,results,0,0);line=list_text(p.pid,results,0,1);col=list_text(p.pid,results,0,2)
  print(name,'hit=',path_text,line,col,flush=True)
  if pathlib.Path(path_text)!=fixture/f'{name}.txt' or line!='2' or col!='1':raise RuntimeError(f'{name}: wrong location')
  u.SendMessageW(results,0x1009,0,0)
 print('Find in Files EOL handling: 3/3 PASS')
 if permission_denied:
  submit_search(p.pid,'needle_after_denied')
  count=0
  for _ in range(100):
   time.sleep(.1);count=u.SendMessageW(results,0x1004,0,0)
   if count:break
  if count!=1 or pathlib.Path(list_text(p.pid,results,0,0))!=readable_hit:
   raise RuntimeError(f'permission-denied subtree prevented later sibling search: count={count}')
  print('Find in Files permission-denied sibling recovery: PASS')
 else:
  print('Find in Files permission-denied sibling recovery: INCONCLUSIVE (ACL denial not enforceable)')
 junction=subprocess.run(
  ['cmd.exe','/c','mklink','/J',str(symlink_path),str(fixture)],
  capture_output=True,text=True
 )
 if junction.returncode==0:
  symlink_created=True
  junction_created=True
 else:
  try:
   os.symlink(fixture,symlink_path,target_is_directory=True)
   symlink_created=True
  except (OSError,NotImplementedError):
   symlink_created=False
 if symlink_created:
  submit_search(p.pid,'needle_after_denied')
  count=0
  for _ in range(100):
   time.sleep(.1);count=u.SendMessageW(results,0x1004,0,0)
   if count:break
  if count!=1 or pathlib.Path(list_text(p.pid,results,0,0))!=readable_hit:
   raise RuntimeError(f'symlink directory was followed or sibling hit was lost: count={count}')
  print('Find in Files symlink non-following: PASS')
 else:
  print('Find in Files symlink non-following: INCONCLUSIVE (symlink creation unavailable)')
 external_dir.mkdir(parents=True,exist_ok=True)
 external_file.write_text('needle_outside_workspace_unique\n',encoding='utf-8')
 external_link=subprocess.run(
  ['cmd.exe','/c','mklink','/J',str(external_junction),str(external_dir)],
  capture_output=True,text=True
 )
 if external_link.returncode==0:
  external_junction_created=True
  submit_search(p.pid,'needle_outside_workspace_unique')
  time.sleep(2.0)
  count=u.SendMessageW(results,0x1004,0,0)
  if count!=0:
   raise RuntimeError(f'Find in Files followed an external directory junction: {count} result(s)')
  print('Find in Files external junction containment: PASS')
 else:
  print('Find in Files external junction containment: INCONCLUSIVE (junction creation unavailable)')
 file_link=subprocess.run(
  ['cmd.exe','/c','mklink',str(external_file_link),str(external_file)],
  capture_output=True,text=True
 )
 if file_link.returncode==0:
  external_file_link_created=True
  submit_search(p.pid,'needle_outside_workspace_unique')
  time.sleep(2.0)
  count=u.SendMessageW(results,0x1004,0,0)
  if count!=0:
   raise RuntimeError(f'Find in Files followed an external file symlink: {count} result(s)')
  print('Find in Files external file-symlink containment: PASS')
 else:
  print('Find in Files external file-symlink containment: INCONCLUSIVE (file symlink creation unavailable)')
 deep_dir=fixture
 for index in range(34):
  deep_dir=deep_dir/f'depth-{index:02d}'
  deep_dir.mkdir()
 (deep_dir/'too-deep.txt').write_text('needle_beyond_depth_limit\n',encoding='utf-8')
 submit_search(p.pid,'needle_beyond_depth_limit')
 time.sleep(2.0)
 count=u.SendMessageW(results,0x1004,0,0)
 if count!=0:
  raise RuntimeError(f'Find in Files traversed beyond depth limit: {count} result(s)')
 print('Find in Files maximum-depth bound: PASS')
finally:
 subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 subprocess.run(['icacls',str(denied_dir),'/reset','/T','/C'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 if junction_created:
  try: os.rmdir(symlink_path)
  except OSError: pass
 elif symlink_created:
  try: symlink_path.unlink()
  except OSError: pass
 if external_junction_created:
  try: os.rmdir(external_junction)
  except OSError: pass
 if external_file_link_created:
  try: external_file_link.unlink()
  except OSError: pass
 shutil.rmtree(external_dir,ignore_errors=True)
 shutil.rmtree(fixture,ignore_errors=True);shutil.rmtree(profile,ignore_errors=True)
