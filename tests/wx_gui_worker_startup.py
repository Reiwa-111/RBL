import os,pathlib,subprocess,time,tempfile,shutil,psutil
root=pathlib.Path.cwd();exe=root/'build/wx-ui-win/rbl-studio-wx.exe';worker=root/'build/wx-ui-win/rbl-studio-worker.exe'
profile=pathlib.Path(tempfile.mkdtemp());(profile/'ui.ini').write_text('[Workspace]\nRoot='+root.as_posix()+'\n')
env=os.environ.copy();env['RBL_STUDIO_CONFIG_DIR']=str(profile);env['RBL_STUDIO_WORKER']=str(worker);env['RBL_PYTHON']=__import__('sys').executable
p=subprocess.Popen([str(exe)],cwd=root,env=env)
try:
 time.sleep(4)
 children=psutil.Process(p.pid).children(recursive=True)
 workers=[c for c in children if c.name().lower()=='rbl-studio-worker.exe']
 print('GUI pid',p.pid,'worker pids',[w.pid for w in workers],'all children',[(c.name(),c.pid) for c in children],flush=True)
 if len(workers)!=1:raise RuntimeError(f'expected exactly one worker after workspace restore, got {len(workers)}')
 print('workspace restore worker startup: 1/1 PASS',flush=True)
finally:
 subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);shutil.rmtree(profile,ignore_errors=True)
