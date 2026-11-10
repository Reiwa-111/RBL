import os,pathlib,subprocess,time,tempfile,shutil
import win32gui,win32con,win32process
root=pathlib.Path.cwd();exe=root/'build/wx-ui-win/rbl-studio-wx.exe';worker=root/'build/wx-ui-win/rbl-studio-worker.exe'
fixture=root/'gui-project-dir-limit';fixture.mkdir(exist_ok=True)
for i in range(1501):(fixture/f'entry-{i:04d}.txt').touch()
profile=pathlib.Path(tempfile.mkdtemp());(profile/'ui.ini').write_text('[Workspace]\nRoot='+fixture.as_posix()+'\n')
env=os.environ.copy();env['RBL_STUDIO_CONFIG_DIR']=str(profile);env['RBL_STUDIO_WORKER']=str(worker);env['RBL_PYTHON']=__import__('sys').executable
p=subprocess.Popen([str(exe)],cwd=root,env=env)
try:
 time.sleep(4);frames=[];win32gui.EnumWindows(lambda h,_:frames.append(h) if win32gui.IsWindowVisible(h) and win32process.GetWindowThreadProcessId(h)[1]==p.pid and win32gui.GetWindowText(h)=='RBL Studio' else None,None)
 trees=[];win32gui.EnumChildWindows(frames[0],lambda h,_:trees.append(h) if win32gui.GetClassName(h)=='SysTreeView32' else None,None)
 count=win32gui.SendMessage(trees[0],0x1105,0,0)
 print('directory entries=1501, rendered tree nodes=',count,flush=True)
 if count!=1502:raise RuntimeError(f'expected root+1500 entries+truncation marker = 1502, got {count}')
 print('per-directory 1500-entry bound: PASS',flush=True)
finally:
 subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);shutil.rmtree(fixture,ignore_errors=True);shutil.rmtree(profile,ignore_errors=True)
