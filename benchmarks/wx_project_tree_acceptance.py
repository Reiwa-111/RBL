import os, pathlib, subprocess, time, tempfile, shutil, psutil
import win32gui, win32con, win32process

root = pathlib.Path.cwd()
exe = root / 'build/wx-ui-win/rbl-studio-wx.exe'
worker = root / 'build/wx-ui-win/rbl-studio-worker.exe'
fixture = root / 'gui-project-tree-acceptance'
fixture.mkdir(exist_ok=True)

for d in range(34):
    folder = fixture / f'dir-{d:02d}'
    folder.mkdir()
    for i in range(1500):
        (folder / f'file-{i:04d}.txt').touch()

profile = pathlib.Path(tempfile.mkdtemp(prefix='rblstudio-tree-'))
(profile / 'ui.ini').write_text('[Workspace]\nRoot=' + fixture.as_posix() + '\n')
env = os.environ.copy()
env['RBL_STUDIO_CONFIG_DIR'] = str(profile)
env['RBL_STUDIO_WORKER'] = str(worker)
env['RBL_PYTHON'] = __import__('sys').executable
p = subprocess.Popen([str(exe)], cwd=root, env=env)

try:
    time.sleep(5)
    frames = []
    win32gui.EnumWindows(lambda h, _: frames.append(h) if win32gui.IsWindowVisible(h)
                         and win32process.GetWindowThreadProcessId(h)[1] == p.pid
                         and win32gui.GetWindowText(h) == 'RBL Studio' else None, None)
    if not frames:
        raise RuntimeError('IDE frame missing')
    trees = []
    win32gui.EnumChildWindows(frames[0], lambda h, _: trees.append(h)
                              if win32gui.GetClassName(h) == 'SysTreeView32' else None, None)
    if not trees:
        raise RuntimeError('project tree control missing')
    tree = trees[0]
    count = win32gui.SendMessage(tree, 0x1105, 0, 0)
    root_item = win32gui.SendMessage(tree, 0x110A, 0, 0)
    child = win32gui.SendMessage(tree, 0x110A, 4, root_item)
    expanded = 0
    peak_rss = 0
    peak_uss = 0
    while child and expanded < 34 and count < 50000:
        win32gui.SendMessage(tree, 0x1102, 2, child)
        time.sleep(.03)
        expanded += 1
        count = win32gui.SendMessage(tree, 0x1105, 0, 0)
        procs = [psutil.Process(p.pid)] + psutil.Process(p.pid).children(recursive=True)
        peak_rss = max(peak_rss, sum(q.memory_info().rss for q in procs if q.is_running()))
        try:
            peak_uss = max(peak_uss, sum(q.memory_full_info().uss for q in procs if q.is_running()))
        except (psutil.AccessDenied, AttributeError):
            pass
        child = win32gui.SendMessage(tree, 0x110A, 1, child)
    print('expanded directories', expanded, 'tree nodes', count, flush=True)
    if count > 50000:
        raise RuntimeError(f'global tree node cap exceeded: {count}')
    if count < 49900 or expanded < 30:
        raise RuntimeError(f'global cap not exercised: nodes={count}, expanded={expanded}')
    print('global project-tree node bound: PASS (<= 50,000)', flush=True)
    print('process-tree peak RSS MiB', round(peak_rss / 1024 / 1024, 1),
          'peak USS MiB', round(peak_uss / 1024 / 1024, 1), flush=True)
    if peak_rss > 512 * 1024 * 1024:
        raise RuntimeError('project tree workload exceeded 512 MiB RSS')
finally:
    subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    shutil.rmtree(fixture, ignore_errors=True)
    shutil.rmtree(profile, ignore_errors=True)
