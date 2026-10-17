/** Bounded native packaging subprocesses; no application payload is executed. */
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {constants} from 'node:fs';

export async function readPackageFile(filename,limit=512*1024*1024) {
  const file=await fs.open(filename,constants.O_RDONLY|(constants.O_NOFOLLOW||0));
  try {
    const before=await file.stat();
    if(!before.isFile()||before.size>limit)throw new Error('Expected a bounded regular packaging file: '+filename);
    const bytes=await file.readFile(),after=await file.stat();
    if(bytes.length!==before.size||before.size!==after.size||before.mtimeMs!==after.mtimeMs)throw new Error('Packaging file changed while reading: '+filename);
    return bytes;
  }finally {await file.close();}
}
export async function writePackageFile(root,relative,bytes,mode=0o644) {
  if(typeof relative!=='string'||!relative||relative.length>4096||/[\\\x00-\x1f\x7f]/.test(relative)||relative.split('/').some(part=>!part||part==='.'||part==='..'||Buffer.byteLength(part)>255))throw new Error('Invalid native package path');
  const destination=path.join(root,...relative.split('/'));
  await fs.mkdir(path.dirname(destination),{recursive:true,mode:0o755});
  const file=await fs.open(destination,'wx',mode);
  try {await file.writeFile(bytes);}finally {await file.close();}
  return destination;
}
export function runPackagingTool(command,args,{cwd,signal,env={},timeout=300000,maxOutput=4*1024*1024,successCodes=[0]}={}) {
  signal?.throwIfAborted();
  if(typeof command!=='string'||!command||command.includes('\0'))throw new Error('Invalid native packaging tool path');
  return new Promise((resolve,reject)=>{
    const ownGroup=process.platform!=='win32';
    const child=spawn(command,args,{cwd,env:{...process.env,...env,LC_ALL:'C',LANG:'C'},stdio:['ignore','pipe','pipe'],shell:false,detached:ownGroup});
    let stdout=[],stderr=[],length=0,failure,timer,killTimer,settled=false;
    const terminate=kind=>{
      if(ownGroup&&child.pid){try{process.kill(-child.pid,kind);}catch(error){if(error.code!=='ESRCH')child.kill(kind);}}
      else child.kill(kind);
    };
    const stop=error=>{failure??=error;terminate('SIGTERM');killTimer??=setTimeout(()=>terminate('SIGKILL'),2000);killTimer.unref();};
    const abort=()=>stop(signal.reason??new DOMException('Native packaging cancelled','AbortError'));
    const finish=(error,value)=>{
      if(settled)return;settled=true;clearTimeout(timer);clearTimeout(killTimer);signal?.removeEventListener('abort',abort);
      if(error)reject(error);else resolve(value);
    };
    signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();
    timer=setTimeout(()=>stop(new Error('Native packaging tool timed out: '+path.basename(command))),timeout);timer.unref();
    for(const [stream,chunks]of [[child.stdout,stdout],[child.stderr,stderr]])stream.on('data',data=>{
      length+=data.length;if(length>maxOutput){stop(new Error('Native packaging tool output exceeded limit: '+path.basename(command)));return;}chunks.push(data);
    });
    child.on('error',error=>finish(error.code==='ENOENT'?new Error('Required native packaging tool was not found: '+command,{cause:error}):error));
    child.on('close',(code,termination)=>{
      const out=Buffer.concat(stdout).toString('utf8'),err=Buffer.concat(stderr).toString('utf8');
      finish(failure??(successCodes.includes(code)?null:new Error(path.basename(command)+' failed ('+(termination??code)+'): '+(err.trim()||out.trim()).slice(-12000))),{stdout:out,stderr:err});
    });
  });
}
/** Resolve links against the target root, never the build host's /lib. */
export async function sysrootPath(root,relative) {
  const parts=relative.replace(/^\/+/, '').split('/').filter(Boolean);let resolved=[];
  for(let links=0;parts.length;){
    const next=parts.shift();if(next==='.')continue;if(next==='..'){if(!resolved.length)throw new Error('Dependency escapes its sysroot');resolved.pop();continue;}
    const location=path.join(root,...resolved,next),info=await fs.lstat(location);
    if(info.isSymbolicLink()){
      if(++links>40)throw new Error('Too many dependency symbolic links');
      const target=await fs.readlink(location);if(target.startsWith('/'))resolved=[];
      parts.unshift(...target.split('/').filter(Boolean));
    }else resolved.push(next);
  }
  return path.join(root,...resolved);
}
export async function packageToolVersion(command,args,settings) {
  const {stdout,stderr}=await runPackagingTool(command,args,{...settings,maxOutput:128*1024,timeout:30000});
  const lines=(stdout.trim()||stderr.trim()).split('\n').map(line=>line.trim());
  return (lines[0].endsWith(':')&&lines[1]?lines.slice(0,2).join(' '):lines[0]).slice(0,512);
}
