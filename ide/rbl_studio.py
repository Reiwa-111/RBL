#!/usr/bin/env python3
from __future__ import annotations
import json, os, re, subprocess, sys, threading, time
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools' / 'rbltool.py'
CONFIG_DIR = Path(os.environ.get('APPDATA', Path.home()/'.config')) / 'RBLStudio'
CONFIG_FILE = CONFIG_DIR / 'settings.json'

THEMES = {
    'Dark': {'bg':'#17151f','fg':'#e9e4f2','editor':'#17151f','panel':'#211d2b','gutter':'#292332','gutter_fg':'#7f748f','accent':'#9d76d8','keyword':'#d89cff','string':'#b8e986','number':'#77c8ff','comment':'#777080','func':'#ffd580','cursor':'#ffffff'},
    'Light': {'bg':'#f3f3f7','fg':'#24222a','editor':'#ffffff','panel':'#ececf2','gutter':'#e5e5eb','gutter_fg':'#77737f','accent':'#6941a5','keyword':'#7c3aed','string':'#3f7d20','number':'#006dba','comment':'#7a7482','func':'#9a5a00','cursor':'#222222'},
    'Nord': {'bg':'#2e3440','fg':'#eceff4','editor':'#2e3440','panel':'#3b4252','gutter':'#434c5e','gutter_fg':'#81a1c1','accent':'#88c0d0','keyword':'#81a1c1','string':'#a3be8c','number':'#b48ead','comment':'#616e82','func':'#ebcb8b','cursor':'#eceff4'},
    'Monokai': {'bg':'#272822','fg':'#f8f8f2','editor':'#272822','panel':'#30312b','gutter':'#1f201b','gutter_fg':'#75715e','accent':'#ae81ff','keyword':'#f92672','string':'#e6db74','number':'#ae81ff','comment':'#75715e','func':'#a6e22e','cursor':'#f8f8f2'},
}

DEFAULTS = {
    'theme':'Dark', 'font_size':12, 'tab_size':4, 'auto_check':True, 'auto_save':False,
    'wsl_mode':'auto', 'wsl_distro':'Ubuntu-24.04', 'show_line_numbers':True, 'remember_window':True,
}


def load_settings():
    try:
        d=json.loads(CONFIG_FILE.read_text(encoding='utf-8'))
        return {**DEFAULTS, **d}
    except Exception:
        return DEFAULTS.copy()

def save_settings(d):
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    CONFIG_FILE.write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding='utf-8')

class EditorTab:
    def __init__(self, app, path=None, content=''):
        self.app=app; self.path=Path(path).resolve() if path else None; self.dirty=False
        self.frame=tk.Frame(app.notebook, bg=app.colors['editor'])
        self.gutter=tk.Text(self.frame,width=5,padx=6,pady=8,bd=0,highlightthickness=0,state='disabled',wrap='none')
        self.text=tk.Text(self.frame,padx=8,pady=8,bd=0,highlightthickness=0,undo=True,wrap='none',tabs=(f'{app.settings["tab_size"]}c',))
        self.scroll=tk.Scrollbar(self.frame, orient='vertical', command=self._yview)
        self.hscroll=tk.Scrollbar(self.frame, orient='horizontal', command=self._xview)
        self.text.configure(yscrollcommand=self._yscroll,xscrollcommand=self.hscroll.set)
        self.gutter.pack(side='left',fill='y'); self.text.pack(side='left',fill='both',expand=True); self.scroll.pack(side='right',fill='y'); self.hscroll.pack(side='bottom',fill='x')
        self.text.insert('1.0',content)
        self.text.bind('<<Modified>>', self._modified)
        self.text.bind('<KeyRelease>', self._keyrelease)
        self.text.bind('<Return>', self._smart_return)
        self.text.bind('<Tab>', self._tab)
        self.text.bind('<Control-s>', lambda e:(app.save_current(), 'break')[1])
        self.text.bind('<F5>', lambda e:(app.run_current(), 'break')[1])
        self.refresh()
    def _yscroll(self,*args):
        self.scroll.set(*args); self.gutter.yview_moveto(args[0])
    def _yview(self,*args):
        self.text.yview(*args); self.gutter.yview(*args)
    def _xview(self,*args): self.text.xview(*args)
    def _modified(self,_=None):
        if self.text.edit_modified():
            self.dirty=True; self.app.update_title(); self.text.edit_modified(False)
    def _keyrelease(self,_):
        self.refresh(); self.app.schedule_check()
    def _smart_return(self,event):
        line=self.text.get('insert linestart','insert')
        m=re.match(r'(\s*)',line); indent=m.group(1)
        stripped=line.strip()
        if stripped.startswith(')') and len(indent) >= self.app.settings['tab_size']:
            indent=indent[:-self.app.settings['tab_size']]
        if line.rstrip().endswith('('):
            indent += ' '*self.app.settings['tab_size']
        self.text.insert('insert','\n'+indent)
        return 'break'
    def _tab(self,event):
        self.text.insert('insert',' '*self.app.settings['tab_size']); return 'break'
    def refresh(self):
        if not self.app.settings.get('show_line_numbers',True): self.gutter.pack_forget()
        else:
            if not self.gutter.winfo_manager(): self.gutter.pack(side='left',fill='y',before=self.text)
        self.apply_colors(); self.highlight(); self.update_gutter()
    def apply_colors(self):
        c=self.app.colors
        self.text.configure(bg=c['editor'],fg=c['fg'],insertbackground=c['cursor'],selectbackground=c['accent'],selectforeground='#ffffff',font=('Consolas' if os.name=='nt' else 'DejaVu Sans Mono',self.app.settings['font_size']))
        self.gutter.configure(bg=c['gutter'],fg=c['gutter_fg'],font=('Consolas' if os.name=='nt' else 'DejaVu Sans Mono',self.app.settings['font_size']))
        for tag,key in [('keyword','keyword'),('string','string'),('number','number'),('comment','comment'),('func','func'),('error','accent')]:
            self.text.tag_configure(tag,foreground=c[key] if key in c else c['accent'])
    def highlight(self):
        w=self.text
        for tag in ('keyword','string','number','comment','func','error'):
            w.tag_remove(tag,'1.0','end')
        ranges=[]
        # comments first
        for m in re.finditer(r'//[^\n]*',w.get('1.0','end-1c')): ranges.append(('comment',m.start(),m.end()))
        text=w.get('1.0','end-1c')
        # strings
        for m in re.finditer(r'"[^"\n]*"',text): ranges.append(('string',m.start(),m.end()))
        for m in re.finditer(r'\b\d+(?:f)?\b',text): ranges.append(('number',m.start(),m.end()))
        for m in re.finditer(r'\b(set|let|func|if|elif|else|return|true|false|and|or|not|for|in)\b',text): ranges.append(('keyword',m.start(),m.end()))
        for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_\u0080-\uffff]*)\s*\(',text): ranges.append(('func',m.start(1),m.end(1)))
        for tag,a,b in ranges:
            start='1.0 + %dc'%a; end='1.0 + %dc'%b; w.tag_add(tag,start,end)
    def update_gutter(self):
        self.gutter.config(state='normal'); self.gutter.delete('1.0','end')
        count=int(self.text.index('end-1c').split('.')[0]); self.gutter.insert('1.0','\n'.join(str(i) for i in range(1,count+1))); self.gutter.config(state='disabled')
    def title(self):
        return (self.path.name if self.path else 'Untitled.rbl') + (' *' if self.dirty else '')
    def get(self): return self.text.get('1.0','end-1c')
    def save(self,path=None):
        if path: self.path=Path(path).resolve()
        if not self.path: return False
        self.path.write_text(self.get(),encoding='utf-8'); self.dirty=False; self.app.update_title(); return True

class SettingsDialog(tk.Toplevel):
    def __init__(self,app):
        super().__init__(app.root); self.app=app; self.title('RBL Studio — Settings'); self.geometry('500x470'); self.transient(app.root); self.grab_set(); self.resizable(False,False)
        f=tk.Frame(self,bg=app.colors['panel'],padx=18,pady=18); f.pack(fill='both',expand=True)
        self.vars={}
        fields=[('theme','Theme',list(THEMES)),('font_size','Font size',None),('tab_size','Tab size',None),('wsl_mode','Windows toolchain (auto/native/wsl)', ['auto','native','wsl']),('wsl_distro','WSL distribution',None)]
        for i,(key,label,choices) in enumerate(fields):
            tk.Label(f,text=label,anchor='w',bg=app.colors['panel'],fg=app.colors['fg']).grid(row=i,column=0,sticky='w',pady=8)
            v=tk.StringVar(value=str(app.settings[key])); self.vars[key]=v
            if choices: w=ttk.Combobox(f,textvariable=v,values=choices,state='readonly',width=25)
            else: w=tk.Entry(f,textvariable=v,width=27)
            w.grid(row=i,column=1,sticky='w',pady=8)
        for j,(key,label) in enumerate([('auto_check','Automatic syntax checks'),('auto_save','Auto-save before run'),('show_line_numbers','Show line numbers')],start=5):
            v=tk.BooleanVar(value=bool(app.settings[key])); self.vars[key]=v; tk.Checkbutton(f,text=label,variable=v,bg=app.colors['panel'],fg=app.colors['fg'],selectcolor=app.colors['gutter'],activebackground=app.colors['panel'],activeforeground=app.colors['fg']).grid(row=j,column=0,columnspan=2,sticky='w',pady=6)
        b=tk.Frame(f,bg=app.colors['panel']); b.grid(row=9,column=0,columnspan=2,pady=18,sticky='e')
        tk.Button(b,text='Cancel',command=self.destroy).pack(side='right',padx=6); tk.Button(b,text='Apply',command=self.apply).pack(side='right',padx=6)
    def apply(self):
        for k,v in self.vars.items():
            val=v.get() if hasattr(v,'get') else v
            if k in ('font_size','tab_size'): val=int(val)
            self.app.settings[k]=val
        save_settings(self.app.settings); self.app.apply_theme(); self.destroy()

class RBLStudio:
    def __init__(self,root,initial=None):
        self.root=root; self.settings=load_settings(); self.colors=THEMES[self.settings['theme']]; self.check_after=None
        self.root.title('RBL Studio'); self.root.geometry('1280x800'); self.root.minsize(900,600)
        self._build_ui(); self.apply_theme()
        if initial: self.open_file(initial)
        else: self.new_file()
    def _build_ui(self):
        self.menu=tk.Menu(self.root)
        filem=tk.Menu(self.menu,tearoff=0); filem.add_command(label='New',accelerator='Ctrl+N',command=self.new_file); filem.add_command(label='Open…',accelerator='Ctrl+O',command=self.open_dialog); filem.add_separator(); filem.add_command(label='Save',accelerator='Ctrl+S',command=self.save_current); filem.add_command(label='Save As…',command=self.save_as); filem.add_separator(); filem.add_command(label='Exit',command=self.root.destroy); self.menu.add_cascade(label='File',menu=filem)
        runm=tk.Menu(self.menu,tearoff=0); runm.add_command(label='Run',accelerator='F5',command=self.run_current); runm.add_command(label='Build ASM',accelerator='Ctrl+B',command=self.build_current); runm.add_command(label='Syntax Check',accelerator='Ctrl+Shift+B',command=self.check_current); runm.add_command(label='Show Generated ASM',command=self.show_asm); runm.add_separator(); runm.add_command(label='Run Tests',accelerator='Ctrl+T',command=self.run_tests); runm.add_command(label='Benchmarks',accelerator='Ctrl+Shift+T',command=self.run_bench); self.menu.add_cascade(label='Run',menu=runm)
        view=tk.Menu(self.menu,tearoff=0); theme=tk.Menu(view,tearoff=0)
        for name in THEMES: theme.add_command(label=name,command=lambda n=name:self.set_theme(n))
        view.add_cascade(label='Theme',menu=theme); view.add_command(label='Settings…',command=self.open_settings); view.add_command(label='Clear Output',command=self.clear_output); self.menu.add_cascade(label='View',menu=view)
        helpm=tk.Menu(self.menu,tearoff=0); helpm.add_command(label='RBL Quick Reference',command=lambda:self.open_file(ROOT/'docs'/'RBL_QUICK_REFERENCE.md')); helpm.add_command(label='Toolchain Doctor',command=lambda:self.run_tool('doctor',None)); helpm.add_command(label='About',command=self.about); self.menu.add_cascade(label='Help',menu=helpm)
        self.root.config(menu=self.menu)
        top=tk.Frame(self.root); top.pack(fill='x');
        for label,cmd in [('New',self.new_file),('Open',self.open_dialog),('Save',self.save_current),('Run ▶',self.run_current),('Build ASM',self.build_current),('Check ✓',self.check_current),('Tests',self.run_tests),('Bench',self.run_bench)]: tk.Button(top,text=label,command=cmd).pack(side='left',padx=3,pady=4)
        self.mainpaned=tk.PanedWindow(self.root,orient='vertical',sashrelief='raised'); self.mainpaned.pack(fill='both',expand=True)
        editor_frame=tk.Frame(self.mainpaned); output_frame=tk.Frame(self.mainpaned); self.mainpaned.add(editor_frame,minsize=350); self.mainpaned.add(output_frame,minsize=180)
        self.notebook=ttk.Notebook(editor_frame); self.notebook.pack(fill='both',expand=True); self.notebook.bind('<<NotebookTabChanged>>',lambda e:self.update_title())
        self.out_notebook=ttk.Notebook(output_frame); self.out_notebook.pack(fill='both',expand=True)
        self.output=tk.Text(self.out_notebook,wrap='none',state='disabled'); self.console=tk.Text(self.out_notebook,wrap='none',state='disabled'); self.problems=tk.Text(self.out_notebook,wrap='none',state='disabled')
        self.out_notebook.add(self.output,text='Run'); self.out_notebook.add(self.console,text='Console'); self.out_notebook.add(self.problems,text='Problems')
        self.status=tk.Label(self.root,anchor='w',padx=8); self.status.pack(fill='x')
        self.root.bind('<Control-n>',lambda e:(self.new_file(),'break')[1]); self.root.bind('<Control-o>',lambda e:(self.open_dialog(),'break')[1]); self.root.bind('<Control-s>',lambda e:(self.save_current(),'break')[1]); self.root.bind('<F5>',lambda e:(self.run_current(),'break')[1]); self.root.bind('<Control-b>',lambda e:(self.build_current(),'break')[1]); self.root.bind('<Control-t>',lambda e:(self.run_tests(),'break')[1]); self.root.bind('<Control-Shift-T>',lambda e:(self.run_bench(),'break')[1]); self.root.bind('<Control-Shift-B>',lambda e:(self.check_current(),'break')[1])
    @property
    def current(self):
        tab=self.notebook.select()
        if not tab:return None
        for obj in getattr(self,'tabs',[]):
            if str(obj.frame)==tab:return obj
        return None
    def new_file(self):
        if not hasattr(self,'tabs'):self.tabs=[]
        tab=EditorTab(self); self.tabs.append(tab); self.notebook.add(tab.frame,text=tab.title()); self.notebook.select(tab.frame); self.update_title()
    def open_dialog(self):
        p=filedialog.askopenfilename(filetypes=[('RBL source','*.rbl'),('All files','*.*')]);
        if p:self.open_file(p)
    def open_file(self,path):
        path=Path(path)
        if not hasattr(self,'tabs'):self.tabs=[]
        for t in self.tabs:
            if t.path and t.path.resolve()==path.resolve(): self.notebook.select(t.frame); return
        try:c=path.read_text(encoding='utf-8')
        except Exception as e:messagebox.showerror('Open failed',str(e));return
        t=EditorTab(self,path,c);self.tabs.append(t);self.notebook.add(t.frame,text=t.title());self.notebook.select(t.frame);self.update_title();self.check_current()
    def save_current(self):
        t=self.current
        if not t:return False
        if not t.path:return self.save_as()
        ok=t.save();self.notebook.tab(t.frame,text=t.title());return ok
    def save_as(self):
        t=self.current
        if not t:return False
        p=filedialog.asksaveasfilename(defaultextension='.rbl',filetypes=[('RBL source','*.rbl')]);
        if not p:return False
        ok=t.save(p);self.notebook.tab(t.frame,text=t.title());return ok
    def update_title(self):
        t=self.current
        self.root.title(f'RBL Studio — {t.title() if t else ""}')
        if t and t.path:self.status.config(text=str(t.path))
    def set_theme(self,n): self.settings['theme']=n;save_settings(self.settings);self.apply_theme()
    def apply_theme(self):
        self.colors=THEMES[self.settings['theme']]; c=self.colors
        self.root.configure(bg=c['bg']); self.status.configure(bg=c['panel'],fg=c['gutter_fg'])
        if hasattr(self,'tabs'):
            for t in self.tabs:t.refresh()
        for w in [self.output,self.console,self.problems]:w.configure(bg=c['editor'],fg=c['fg'],insertbackground=c['cursor'],font=('Consolas' if os.name=='nt' else 'DejaVu Sans Mono',self.settings['font_size']))
        self._style_ttk()
    def _style_ttk(self):
        s=ttk.Style();
        try:s.theme_use('clam')
        except Exception:pass
        s.configure('TNotebook',background=self.colors['panel']);s.configure('TNotebook.Tab',padding=(10,5))
    def open_settings(self): SettingsDialog(self)
    def append_output(self,text,channel='Run'):
        w={'Run':self.output,'Console':self.console,'Problems':self.problems}.get(channel,self.output);w.configure(state='normal');w.insert('end',text);w.see('end');w.configure(state='disabled');self.out_notebook.select(w)
    def clear_output(self):
        for w in [self.output,self.console,self.problems]:w.configure(state='normal');w.delete('1.0','end');w.configure(state='disabled')
    def run_tool(self,cmd,arg=None,done=None):
        def job():
            args=[sys.executable,str(TOOL),cmd]
            if arg:args.append(str(arg))
            p=subprocess.run(args,text=True,capture_output=True,cwd=str(ROOT))
            def ui():
                self.append_output(p.stdout or '', 'Run' if cmd!='check' else 'Problems');
                if p.stderr:self.append_output(p.stderr,'Problems' if cmd=='check' else 'Run')
                if done:done(p.returncode)
            self.root.after(0,ui)
        threading.Thread(target=job,daemon=True).start()
    def run_current(self):
        t=self.current
        if not t:return
        if self.settings['auto_save'] or t.dirty:self.save_current()
        if not t.path:messagebox.showinfo('Run','Save the file first.');return
        self.clear_output();self.append_output(f'> Run {t.path.name}\n');self.run_tool('run',t.path,lambda rc:self.append_output(f'\n[process exited with code {rc}]\n'))
    def build_current(self):
        t=self.current
        if not t:return
        if t.dirty:self.save_current()
        if not t.path:return
        self.clear_output();self.append_output(f'> Build direct ASM: {t.path.name}\n');self.run_tool('build',t.path,lambda rc:self.append_output(f'\n[build exit {rc}]\n'))
    def check_current(self):
        t=self.current
        if not t or not t.path:return
        def done(rc):
            self.append_output('[syntax OK]\n' if rc==0 else '[syntax errors]\n','Problems')
            self.parse_error_highlight()
        self.run_tool('check',t.path,done)
    def show_asm(self):
        t=self.current
        if not t or not t.path:return
        def done(rc):
            if rc==0:
                asm=ROOT/'build'/t.path.stem/(t.path.stem+'.s')
                if asm.exists():self.open_file(asm)
        self.run_tool('asm',t.path,done)
    def run_tests(self):
        self.clear_output();self.append_output('> Running RBL regression suite\n');
        threading.Thread(target=self._test_job,daemon=True).start()
    def _test_job(self):
        p=subprocess.run([sys.executable,str(ROOT/'tests'/'run_tests.py')],text=True,capture_output=True,cwd=str(ROOT))
        self.root.after(0,lambda:self._done_process(p))
    def run_bench(self):
        self.clear_output();self.append_output('> Running benchmarks\n');
        threading.Thread(target=self._bench_job,daemon=True).start()
    def _bench_job(self):
        p=subprocess.run([sys.executable,str(ROOT/'benchmarks'/'run_benchmarks.py')],text=True,capture_output=True,cwd=str(ROOT))
        self.root.after(0,lambda:self._done_process(p))
    def _done_process(self,p):
        self.append_output(p.stdout or '', 'Run');
        if p.stderr:self.append_output(p.stderr,'Problems')
        self.append_output(f'\n[exit {p.returncode}]\n','Run')
    def parse_error_highlight(self):
        # Lightweight parser diagnostics: locate first "file:line:col" diagnostic from tool output.
        t=self.current
        if not t:return
        txt=self.problems.get('1.0','end-1c'); m=re.search(r':(\d+):(\d+):',txt)
        t.text.tag_remove('error','1.0','end')
        if m:
            line=int(m.group(1)); t.text.tag_add('error',f'{line}.0',f'{line}.end')
    def schedule_check(self):
        if not self.settings['auto_check']:return
        if self.check_after:self.root.after_cancel(self.check_after)
        self.check_after=self.root.after(650,self.check_current)
    def about(self):
        messagebox.showinfo('RBL Studio','RBL Studio\nDirect x86-64 ASM toolchain\n\nLinux: ELF via as + ld + libc.\nWindows: PE via as + gcc (linker driver) + C runtime with sysv_abi.\nNo C code is generated for user programs on either target.\n\nEditor + syntax checks + runner + tests + benchmarks + settings.')

def main():
    root=tk.Tk(); app=RBLStudio(root,sys.argv[1] if len(sys.argv)>1 else None); root.mainloop()
if __name__=='__main__':main()
