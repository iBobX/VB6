/** Resolve the target ELF dependency graph without using ldd or executing it. */
import fs from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {readPackageFile,writePackageFile,runPackagingTool,sysrootPath} from './native-distribution-tools.mjs';

const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const hostLibraries=new Set(['libc.so.6','libm.so.6','libdl.so.2','libpthread.so.0','librt.so.1','libresolv.so.2','libutil.so.1','ld-linux-x86-64.so.2','ld-linux-aarch64.so.1']);
export async function bundleAppImageDependencies({appdir,executable,compiler,toolchain,signal}) {
  const sysroot=await fs.realpath(toolchain.sysroot??'/'),readelf=toolchain.readelf??'readelf',machine=compiler.architecture==='arm64'?183:62;
  const triple=compiler.architecture==='arm64'?'aarch64-linux-gnu':'x86_64-linux-gnu';
  const directories=['usr/lib/'+triple,'lib/'+triple,'usr/lib64','lib64','usr/lib','lib'];
  const files=new Map(),libraries=new Map(),external=new Set(),visited=new Set(),queue=[],caches=[];let total=0,minimumGlibc=[2,39],licenseFiles=0;
  const settings={signal,cwd:appdir,timeout:30000};
  const add=async(relative,bytes,mode=0o644)=>{
    signal?.throwIfAborted();const digest=hash(bytes),before=files.get(relative);
    if(before){if(before.sha256!==digest)throw new Error('Conflicting AppImage dependency path: '+relative);return;}
    if(files.size>=50000||(total+=bytes.length)>512*1024*1024)throw new Error('AppImage dependencies exceed 50,000 files or 512 MiB');
    await writePackageFile(appdir,relative,bytes,mode);files.set(relative,{path:relative,bytes:bytes.length,sha256:digest,mode});
  };
  const source=async(relative)=>sysrootPath(sysroot,relative);
  const maybe=async(relative)=>{try{return await source(relative);}catch(error){if(['ENOENT','ENOTDIR'].includes(error.code))return null;throw error;}};
  async function inspect(filename) {
    const bytes=await readPackageFile(filename,128*1024*1024);
    if(bytes.length<64||bytes[0]!==127||bytes.toString('ascii',1,4)!=='ELF'||bytes[4]!==2||bytes[5]!==1||bytes.readUInt16LE(18)!==machine)throw new Error('AppImage dependency has the wrong ELF architecture: '+filename);
    const text=(await runPackagingTool(readelf,['--dynamic','--version-info','--wide',filename],settings)).stdout;
    for(const match of text.matchAll(/GLIBC_(\d+)\.(\d+)/g)){const next=[Number(match[1]),Number(match[2])];if(next[0]>minimumGlibc[0]||next[0]===minimumGlibc[0]&&next[1]>minimumGlibc[1])minimumGlibc=next;}
    const needed=[...text.matchAll(/\(NEEDED\).*?\[([^\]]+)\]/g)].map(match=>match[1]);
    if(needed.some(name=>!name||name.length>255||/[\/\\\x00-\x20\x7f]/.test(name)))throw new Error('Invalid ELF dependency name');
    const runpaths=[...text.matchAll(/\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]/g)].flatMap(match=>match[1].split(':')).filter(Boolean);
    return {bytes,needed,runpaths};
  }
  async function resolve(name,from,runpaths) {
    const relative=path.relative(sysroot,from),origin=path.posix.dirname(relative.split(path.sep).join('/'));
    const extra=runpaths.map(value=>value.replace(/\$\{ORIGIN\}|\$ORIGIN/g,origin).replace(/^\//,''));
    for(const directory of [...extra,...directories]){
      if(directory.includes('$'))continue;
      const found=await maybe(path.posix.join(directory,name));if(found)return found;
    }
    throw new Error('Missing AppImage dependency '+name+' in target sysroot '+sysroot);
  }
  async function enqueue(filename,destination) {
    const real=await fs.realpath(filename),info=await inspect(real);
    if(destination){await add(destination,info.bytes,0o755);libraries.set(destination,{path:destination,sha256:hash(info.bytes),bytes:info.bytes.length,source:path.relative(sysroot,real).split(path.sep).join('/')});}
    if(visited.has(real))return;visited.add(real);
    for(const name of info.needed){
      if(hostLibraries.has(name)){external.add(name);continue;}
      const library=await resolve(name,real,info.runpaths),target='usr/lib/'+name;
      if(!files.has(target)&&!queue.some(item=>item.destination===target))queue.push({filename:library,destination:target});
    }
  }
  await enqueue(executable);
  const moduleRoots=[];
  for(const directory of directories){
    for(const tail of ['gdk-pixbuf-2.0','gio/modules',compiler.toolkit==='gtk4'?'gtk-4.0':'gtk-3.0']){
      const relative=directory+'/'+tail,location=await maybe(relative);if(!location)continue;
      if(moduleRoots.some(item=>item.location===location))continue;moduleRoots.push({relative,location});
    }
  }
  async function walk(relative,{filter=()=>true,modules=false,depth=0}={}) {
    if(depth>24)throw new Error('AppImage dependency directory nesting exceeds 24');
    const location=await maybe(relative);if(!location)return;
    const info=await fs.stat(location);
    if(info.isDirectory()){
      for(const entry of (await fs.readdir(location)).sort())await walk(relative+'/'+entry,{filter,modules,depth:depth+1});
    }else if(info.isFile()&&filter(relative)){
      const bytes=await readPackageFile(location,128*1024*1024);
      await add(relative,bytes,modules&&/\.so(?:\.|$)/.test(relative)?0o755:0o644);
      if(modules&&/\.so(?:\.|$)/.test(relative))await enqueue(location);
      if(/\/(?:loaders|immodules)\.cache$/.test(relative))caches.push({path:relative,text:bytes.toString('utf8')});
      if(/\/copyright$/.test(relative))licenseFiles++;
    }
  }
  for(const root of moduleRoots)await walk(root.relative,{modules:true,filter:name=>/\.so(?:\.[0-9.]+)?$|\.cache$/.test(name)});
  while(queue.length){signal?.throwIfAborted();const next=queue.shift();await enqueue(next.filename,next.destination);}
  for(const relative of ['usr/share/glib-2.0/schemas','usr/share/icons/Adwaita','usr/share/icons/hicolor/index.theme','usr/share/mime','usr/share/themes/Default','usr/share/fonts/truetype/dejavu','usr/share/common-licenses'])await walk(relative);
  // Package copyright files can refer to /usr/share/common-licenses; retain both
  // the complete notices and those license texts from the selected target root.
  await walk('usr/share/doc',{filter:name=>/\/copyright$/.test(name)});
  if(!licenseFiles)throw new Error('AppImage sysroot is missing /usr/share/doc/*/copyright license notices');
  await add('etc/fonts/fonts.conf',Buffer.from('<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">\n<fontconfig><include ignore_missing="yes">/etc/fonts/fonts.conf</include><dir prefix="relative">../../usr/share/fonts</dir></fontconfig>\n'));
  const cacheFiles=[];
  for(const cache of caches){
    const chunks=[];let plain='';
    for(const line of cache.text.split('\n')){
      const match=line.match(/^"(\/[^"\n]+\.so(?:\.[0-9.]+)?)"\s*$/);
      if(!match){plain+=line+'\n';continue;}
      const relative=match[1].slice(1);
      if(!files.has(relative))throw new Error('AppImage loader cache names an unbundled module: '+match[1]);
      if(plain){chunks.push({text:plain});plain='';}chunks.push({module:relative});
    }
    if(plain)chunks.push({text:plain});cacheFiles.push({path:cache.path,chunks});
  }
  return {files:[...files.values()].sort((a,b)=>a.path.localeCompare(b.path)),libraries:[...libraries.values()].sort((a,b)=>a.path.localeCompare(b.path)),
    externalLibraries:[...external].sort(),minimumGlibc:minimumGlibc.join('.'),bytes:total,licenseFiles,caches:cacheFiles,
    gtkPaths:moduleRoots.filter(item=>/\/gtk-[34]\.0$/.test(item.relative)).map(item=>item.relative),
    gioPaths:moduleRoots.filter(item=>/\/gio\/modules$/.test(item.relative)).map(item=>item.relative)};
}
