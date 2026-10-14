"""Local integration test: runs isolated Noctra copies, never uploads a release."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import time

U=C.WinDLL('user32',use_last_error=True)
K=C.WinDLL('kernel32',use_last_error=True)
CB=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
U.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
U.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
U.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
K.CreateEventW.argtypes=[C.c_void_p,W.BOOL,W.BOOL,W.LPCWSTR];K.CreateEventW.restype=W.HANDLE
K.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];K.CloseHandle.argtypes=[W.HANDLE]

def windows(pid,title):
    out=[]
    @CB
    def collect(h,l):
        found=W.DWORD();U.GetWindowThreadProcessId(h,C.byref(found))
        text=C.create_unicode_buffer(256);U.GetWindowTextW(h,text,256)
        if found.value==pid and text.value==title:out.append(h)
        return True
    U.EnumWindows(collect,0)
    return out

def close(proc):
    if proc.poll() is not None:return
    for h in windows(proc.pid,'NOCTRA - External')+windows(proc.pid,'Noctra update failed'):
        U.PostMessageW(h,0x10,0,0)
    try:proc.wait(timeout=12)
    except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=10)

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def run_case(root,exe,payload,signer,case):
    folder=root/case;folder.mkdir()
    target=folder/'Noctra.exe';shutil.copyfile(exe,target)
    token=secrets.token_hex(16);stage=folder/('Noctra-update-'+token);stage.mkdir()
    shutil.copyfile(exe,stage/'Updater.exe');shutil.copyfile(payload,stage/'Noctra.pending.exe')
    sequence=3 if case=='success' else 4
    subprocess.run([str(signer),'sign',str(payload),str(sequence),'1.2.0','v1.2.0','365',str(stage/'update.json')],check=True)
    env=dict(os.environ);env['LOCALAPPDATA']=str(folder/'local-user');(folder/'local-user').mkdir()
    parent=subprocess.Popen([str(target)],cwd=folder,env=env)
    helper=None;ready=K.CreateEventW(None,True,False,'Local\\NoctraUpdateReady-'+token)
    assert ready,'cannot create parent ready event'
    try:
        deadline=time.monotonic()+15
        while not windows(parent.pid,'NOCTRA - External'):
            assert parent.poll() is None,'parent failed startup'
            assert time.monotonic()<deadline,'parent window unavailable'
            time.sleep(.1)
        helper=subprocess.Popen([str(stage/'Updater.exe'),'--apply-update',str(parent.pid)],cwd=folder,env=env)
        assert K.WaitForSingleObject(ready,10000)==0,'helper failed validation/handshake'
        close(parent);assert parent.returncode==0,'parent did not gracefully exit'
        deadline=time.monotonic()+30
        backup=stage/'Noctra.previous.exe';rejected=stage/'Noctra.rejected.exe'
        while time.monotonic()<deadline:
            if case=='success' and helper.poll()==0 and backup.exists():break
            if case=='rollback' and rejected.exists() and not backup.exists():break
            time.sleep(.1)
        if case=='success':
            assert helper.poll()==0,'successful helper did not exit 0'
            assert digest(target)==digest(payload),'replacement differs'
            assert digest(backup)==digest(exe),'backup not preserved'
            assert (folder/'local-user/Noctra/update-highwater.txt').read_text()=='3','highwater not committed'
        else:
            assert rejected.exists(),'failed release was not rolled back'
            assert digest(target)==digest(exe),'previous release not restored'
            assert digest(rejected)==digest(payload),'rejected payload not preserved'
            assert not (folder/'local-user/Noctra/update-highwater.txt').exists(),'failed release persisted highwater'
        print('PASS real helper:',case)
    finally:
        K.CloseHandle(ready);close(parent)
        if helper:close(helper)
        # Close only child/restored menu windows belonging to our isolated folder.
        subprocess.run(['powershell.exe','-NoProfile','-Command',
            "$folder="+"'"+str(folder).replace("'","''")+"'; Get-Process Noctra -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $folder 'Noctra.exe')} | ForEach-Object {[void]$_.CloseMainWindow(); if(-not $_.WaitForExit(10000)){Stop-Process -Id $_.Id}}"],check=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',default='dist/Noctra.exe');p.add_argument('--signer',default='build/Release/noctra_release.exe');p.add_argument('--failure',default='build/Release/noctra_update_failure.exe');a=p.parse_args()
    root=Path('artifacts')/('update-flow-'+secrets.token_hex(8));root.mkdir()
    run_case(root.resolve(),Path(a.exe).resolve(),Path(a.exe).resolve(),Path(a.signer).resolve(),'success')
    run_case(root.resolve(),Path(a.exe).resolve(),Path(a.failure).resolve(),Path(a.signer).resolve(),'rollback')
