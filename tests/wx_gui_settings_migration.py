import os,pathlib,subprocess,time,tempfile,json,shutil,re,ctypes
import win32gui,win32con,win32process
SendMessageW=ctypes.windll.user32.SendMessageW
SendMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_void_p]
SendMessageW.restype=ctypes.c_ssize_t
root=pathlib.Path.cwd();build_dir=pathlib.Path(os.environ.get('RBL_STUDIO_GUI_BUILD_DIR',root/'build/wx-ui-win'));exe=build_dir/'rbl-studio-wx.exe';worker=build_dir/'rbl-studio-worker.exe'
cases=[
	('valid',{'theme':'Nord','font_size':17,'tab_size':6,'show_line_numbers':False,'soft_wrap':True,'indent_guides':False,'auto_check':False,'auto_save':True,'syntax_highlighting':False,'wsl_mode':'wsl','wsl_distro':'Ubuntu-24.04'},{'Theme':'Nord','EditorColorScheme':'Nord','FontSize':'17','TabSize':'6','LineNumbers':'0','SoftWrap':'1','IndentGuides':'0','AutoCheck':'0','AutoSave':'1','SyntaxHighlighting':'0','Mode':'wsl','WslDistribution':'Ubuntu-24.04'}),
 ('oversized',{'font_size':999999999999999999999999999999999999,'tab_size':999999999999999999999999999999999999,'auto_check':False,'soft_wrap':True},{'FontSize':'11','TabSize':'4','AutoCheck':'0','SoftWrap':'1'}),
 ('negative',{'font_size':-999,'tab_size':-5},{'FontSize':'11','TabSize':'4'}),
 ('malformed',None,{'FontSize':'11','TabSize':'4'}),
]
def frame_for(pid):
 a=[];win32gui.EnumWindows(lambda h,_:a.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==pid and win32gui.GetWindowText(h)=='RBL Studio' else None,None)
 return a[0] if a else 0
def kill_tree(p):
 if p.poll() is None:subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 p.wait(timeout=10)
for name,payload,expected in cases:
 profile=pathlib.Path(tempfile.mkdtemp(prefix='rblstudio-config-'));config=profile/'RBLStudio';config.mkdir()
 if name=='malformed':(config/'settings.json').write_text('{"font_size":oops, broken json',encoding='utf-8')
 else:(config/'settings.json').write_text(json.dumps(payload),encoding='utf-8')
 env=os.environ.copy();env['RBL_STUDIO_CONFIG_DIR']=str(config);env['RBL_STUDIO_WORKER']=str(worker);env['RBL_PYTHON']=__import__('sys').executable
 p=subprocess.Popen([str(exe)],cwd=root,env=env)
 try:
  time.sleep(3)
  if not frame_for(p.pid):raise RuntimeError(f'{name}: GUI startup failed')
  ini=config/'ui.ini'
  if not ini.is_file():raise RuntimeError(f'{name}: ui.ini migration output missing')
  data=ini.read_text(encoding='utf-8',errors='replace')
  actual={k:(re.search(r'^\s*'+k+r'\s*=\s*(.*?)\s*$',data,re.M|re.I).group(1) if re.search(r'^\s*'+k+r'\s*=\s*(.*?)\s*$',data,re.M|re.I) else None) for k in expected}
  if any(actual[k]!=v for k,v in expected.items()):raise RuntimeError(f'{name}: migration mismatch {actual}')
  kill_tree(p)
  p2=subprocess.Popen([str(exe)],cwd=root,env=env)
  try:
   time.sleep(3);frame=frame_for(p2.pid)
   if not frame:raise RuntimeError(f'{name}: persisted profile startup failed')
   win32gui.PostMessage(frame,win32con.WM_COMMAND,6024,0);time.sleep(.4)
   ds=[];win32gui.EnumWindows(lambda h,_:ds.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==p2.pid and win32gui.GetWindowText(h)=='RBL Studio Settings' else None,None)
   if not ds:raise RuntimeError(f'{name}: Settings dialog missing')
   kids=[];win32gui.EnumChildWindows(ds[0],lambda h,_:kids.append(h),None)
   spins=[h for h in kids if win32gui.GetClassName(h)=='msctls_updown32']
   checks={win32gui.GetWindowText(h):str(win32gui.SendMessage(h,0xF0,0,0)) for h in kids if win32gui.GetClassName(h)=='Button' and win32gui.GetDlgCtrlID(h)<0}
   loaded={'FontSize':str(ctypes.windll.user32.SendMessageW(spins[0],0x472,0,0)),'TabSize':str(ctypes.windll.user32.SendMessageW(spins[1],0x472,0,0)),
    'LineNumbers':checks.get('Show line numbers',''),'SoftWrap':checks.get('Soft wrap long lines',''),
    'IndentGuides':checks.get('Show indentation guides',''),'AutoCheck':checks.get('Check RBL syntax after edits',''),
    'AutoSave':checks.get('Save before Run / Build / Assembly',''),'SyntaxHighlighting':checks.get('Enable RBL syntax highlighting','')}
   choices=[h for h in kids if win32gui.GetClassName(h)=='ComboBox']
   if choices:
    selected=SendMessageW(choices[0],0x147,0,None)
    length=SendMessageW(choices[0],0x149,selected,None)
    text_buffer=ctypes.create_unicode_buffer(length+1)
    SendMessageW(choices[0],0x148,selected,ctypes.byref(text_buffer))
    loaded['Theme']=text_buffer.value
   if len(choices)>1:
    selected=SendMessageW(choices[1],0x147,0,None)
    length=SendMessageW(choices[1],0x149,selected,None)
    text_buffer=ctypes.create_unicode_buffer(length+1)
    SendMessageW(choices[1],0x148,selected,ctypes.byref(text_buffer))
    loaded['EditorColorScheme']=text_buffer.value
   if len(choices)>2:
    selected=SendMessageW(choices[2],0x147,0,None)
    length=SendMessageW(choices[2],0x149,selected,None)
    text_buffer=ctypes.create_unicode_buffer(length+1)
    SendMessageW(choices[2],0x148,selected,ctypes.byref(text_buffer))
    loaded['Mode']=text_buffer.value
   edits=[h for h in kids if win32gui.GetClassName(h)=='Edit']
   if edits:loaded['WslDistribution']=win32gui.GetWindowText(edits[-1])
   for key,value in expected.items():
    if loaded[key]!=value:raise RuntimeError(f'{name}: persisted UI value {key} expected {value}, got {loaded[key]}')
   print(name,'migration=PASS persistence=PASS loaded=',{k:loaded[k] for k in expected},flush=True)
  finally:kill_tree(p2)
 finally:
  kill_tree(p);shutil.rmtree(profile,ignore_errors=True)
print('settings legacy migration + UI persistence: 4/4 PASS')
