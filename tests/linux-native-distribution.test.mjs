import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {createHash} from 'node:crypto';
import {loadAppImageRuntime,inspectAppImageRuntime} from '../packages/linux-native/src/appimage-runtime.mjs';
import {LINUX_NATIVE_DISTRIBUTION_FORMATS,LINUX_FLATPAK_DEFAULT_PERMISSIONS} from '../packages/linux-native/src/distribution-targets.js';
import {runPackagingTool,sysrootPath,readPackageFile,writePackageFile} from '../packages/linux-native/src/native-distribution-tools.mjs';
import {linuxNativeDistributionOptions,packageLinuxApplicationNative} from '../packages/linux-native/src/native-distribution.mjs';
import {bundleAppImageDependencies} from '../packages/linux-native/src/appimage-dependencies.mjs';
import {exportLinuxApplication} from '../packages/linux-native/dist/index.js';
import {exportLinuxProject} from '../packages/linux-native/src/node.mjs';
const project={schema:1,name:'Native Distribution',startup:'Sub Main',modules:[{name:'MainModule',kind:'module',code:'Public Sub Main()\nDebug.Print "LINUX_NATIVE_DISTRIBUTION_OK"\nEnd Sub'}]};

test('native distribution capabilities identify the Linux tool authority and both architectures',()=>{
  assert.deepEqual(Object.keys(LINUX_NATIVE_DISTRIBUTION_FORMATS),['appimage','flatpak']);
  for(const format of Object.values(LINUX_NATIVE_DISTRIBUTION_FORMATS)){
    assert.equal(format.authority,'node-native');assert.equal(format.platform,'linux');assert.deepEqual(format.architectures,['x64','arm64']);
    assert.ok(Object.isFrozen(format));assert.ok(Object.isFrozen(format.tools));
  }
});
test('Flatpak defaults are bounded, versioned and avoid broad host access',()=>{
  const options=linuxNativeDistributionOptions({format:'flatpak'},'Zażółć Native App');
  assert.equal(options.runtime,'org.gnome.Platform');assert.equal(options.runtimeVersion,'50');
  assert.match(options.appId,/^org\.vb6studio\./);assert.equal(options.branch,'stable');
  assert.deepEqual(options.flatpakPermissions,LINUX_FLATPAK_DEFAULT_PERMISSIONS);
  assert.ok(!options.flatpakPermissions.some(value=>value.includes('filesystem=')||value==='share=network'));
  assert.ok(Object.isFrozen(options.flatpakPermissions));
  assert.deepEqual(linuxNativeDistributionOptions({format:'flatpak',appId:'org.example.Test',flatpakPermissions:[]}).flatpakPermissions,[]);
});
test('native package options reject identity, INI, shell and permission injection',()=>{
  for(const options of [{format:'zip'},{format:'flatpak',appId:'org.example.Bad;exec'}, {format:'flatpak',appId:'org..bad'},
    {format:'flatpak',runtime:'org.freedesktop.Platform'},{format:'flatpak',runtimeVersion:'49'},{format:'flatpak',runtimeVersion:'50beta'},
    {format:'flatpak',runtimeVersion:'50\ncommand=evil'},{format:'flatpak',runtimeRepository:'http://example.invalid/repo'},
    {format:'flatpak',runtimeRepository:'https://user:secret@example.invalid/repo'},{format:'flatpak',runtimeRepository:'https://example.invalid/repo#fragment'},
    {format:'flatpak',branch:'../stable'},{format:'flatpak',flatpakPermissions:['filesystem=host']},
    {format:'flatpak',flatpakPermissions:['share=network','share=network']},{format:'flatpak',flatpakPermissions:['socket=x11','socket=fallback-x11']},
    {format:'appimage',appId:'org.example.Test'},{format:'appimage',unexpected:true}])assert.throws(()=>linuxNativeDistributionOptions(options),JSON.stringify(options));
});
test('Flatpak identity fits all exported filesystem component limits',()=>{
  const id='org.example.'+'A'.repeat(230);assert.equal(id.length,242);
  assert.equal(linuxNativeDistributionOptions({format:'flatpak',appId:id}).appId,id);
  assert.throws(()=>linuxNativeDistributionOptions({format:'flatpak',appId:id+'A'}),/242 characters/);
});
for(const architecture of ['x64','arm64'])test('pinned '+architecture+' AppImage runtime has a verified static type-2 ELF header',async()=>{
  const runtime=await loadAppImageRuntime(architecture),layout=inspectAppImageRuntime(runtime.bytes,architecture);
  assert.equal(layout.sha256,createHash('sha256').update(runtime.bytes).digest('hex'));
  assert.equal(layout.bytes,runtime.bytes.length);assert.equal(layout.type,2);assert.equal(layout.staticallyLinked,true);
  assert.equal(runtime.manifest.release,'20251108');assert.match(runtime.manifest.source,/dd6cebedcbddde9c82f89b011e8e1d40b6e43868$/);
  assert.throws(()=>inspectAppImageRuntime(runtime.bytes,architecture==='x64'?'arm64':'x64'),/architecture/);
  assert.throws(()=>inspectAppImageRuntime(Buffer.concat([runtime.bytes,Buffer.alloc(4)]),architecture),/offset/);
  const bytes=Buffer.from(runtime.bytes);bytes[8]=0;assert.throws(()=>inspectAppImageRuntime(bytes,architecture),/type-2/);
});
test('custom AppImage runtimes require a matching digest and reject links',async t=>{
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'linux-appimage-header-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));
  const runtime=await loadAppImageRuntime('x64'),filename=path.join(directory,'runtime');await fs.writeFile(filename,runtime.bytes);
  await assert.rejects(loadAppImageRuntime('x64',{runtimePath:filename}),/requires appimageRuntimeSha256/);
  await assert.rejects(loadAppImageRuntime('x64',{runtimePath:filename,runtimeSha256:'0'.repeat(64)}),/SHA256/);
  assert.equal((await loadAppImageRuntime('x64',{runtimePath:filename,runtimeSha256:runtime.manifest.sha256})).manifest.release,'custom');
  if(process.platform!=='win32'){
    const linked=path.join(directory,'linked');await fs.symlink(filename,linked);
    await assert.rejects(loadAppImageRuntime('x64',{runtimePath:linked,runtimeSha256:runtime.manifest.sha256}),error=>error.code==='ELOOP');
  }
});
test('target sysroot links resolve inside the target root and reject escapes',async t=>{
  if(process.platform==='win32'){t.skip('POSIX symbolic-link semantics');return;}
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'linux-sysroot-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));
  await fs.mkdir(path.join(directory,'usr/lib'),{recursive:true});await fs.writeFile(path.join(directory,'usr/lib/test.so'),'target');
  await fs.symlink('/usr/lib',path.join(directory,'lib'));
  assert.equal(await sysrootPath(directory,'lib/test.so'),path.join(directory,'usr/lib/test.so'));
  await fs.symlink('../../outside',path.join(directory,'escape'));
  await assert.rejects(sysrootPath(directory,'escape'),/escapes/);
  await fs.symlink('loop',path.join(directory,'loop'));
  await assert.rejects(sysrootPath(directory,'loop'),/Too many/);
});
test('native packaging uses literal tool arguments and bounds subprocess output',async()=>{
  const literal='literal $(touch should-not-exist); `uname`\nargument';
  const windows=process.platform==='win32';
  const result=await runPackagingTool(windows?process.execPath:'/usr/bin/printf',windows?['-e','process.stdout.write(process.argv[1])',literal]:['%s',literal]);
  assert.equal(result.stdout,literal);
  await assert.rejects(runPackagingTool(windows?process.execPath:'/usr/bin/printf',windows?['-e','process.stdout.write("x".repeat(8192))']:['%8192s',''],{maxOutput:128}),/output exceeded/);
  const controller=new AbortController();const pending=runPackagingTool(windows?process.execPath:'/bin/sleep',windows?['-e','setInterval(()=>{},1000)']:['30'],{signal:controller.signal});controller.abort();
  await assert.rejects(pending,{name:'AbortError'});
  await assert.rejects(runPackagingTool('vb6-required-packager-that-is-not-installed',[]),/Required native packaging tool/);
});
test('native packaging cancellation terminates owned descendants that ignore SIGTERM',async t=>{
  if(process.platform!=='linux'){t.skip('Linux process-group and /proc semantics');return;}
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'linux-package-cancellation-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));
  const pidFile=path.join(directory,'descendant.pid'),controller=new AbortController();
  const pending=runPackagingTool('/bin/sh',['-c','trap "" TERM; /bin/sleep 30 & child=$!; printf "%s\\n" "$child" > "$1"; wait','vb6-cancellation-fixture',pidFile],{signal:controller.signal,timeout:10000});
  const rejected=assert.rejects(pending,{name:'AbortError'});let child,problem;
  try {
    for(let attempt=0;attempt<200;attempt++){
      try {child=Number((await fs.readFile(pidFile,'utf8')).trim());break;}catch(error){if(error.code!=='ENOENT')throw error;await new Promise(resolve=>setTimeout(resolve,10));}
    }
    assert.ok(Number.isInteger(child)&&child>0,'Owned descendant started');
  }catch(error){problem=error;}
  finally {controller.abort();await rejected;}
  if(problem)throw problem;
  try {assert.match(await fs.readFile('/proc/'+child+'/status','utf8'),/^State:\s+Z\b/m,'Cancelled descendant is not running');}
  catch(error){if(error.code!=='ENOENT')throw error;}
});
test('native package writers reject path traversal and preserve reserved outputs',async t=>{
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'linux-native-writer-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));
  await assert.rejects(writePackageFile(directory,'../escape','bad'),/Invalid native package path/);
  await writePackageFile(directory,'files/app','first');await assert.rejects(writePackageFile(directory,'files/app','second'),error=>error.code==='EEXIST');
  assert.equal((await readPackageFile(path.join(directory,'files/app'))).toString(),'first');
  await assert.rejects(readPackageFile(path.join(directory,'files/app'),2),/bounded regular/);
});
test('native package validation rejects damage and cancellation before invoking tools',async()=>{
  const app=await exportLinuxApplication(project),bad=app.bytes.slice();bad[0]^=1;
  await assert.rejects(packageLinuxApplicationNative({...app,bytes:bad},{format:'flatpak'},{toolchain:{flatpak:'never-execute-me'}}));
  await assert.rejects(packageLinuxApplicationNative(app,{format:'flatpak'},{signal:AbortSignal.abort()}),{name:'AbortError'});
  await assert.rejects(packageLinuxApplicationNative(app,{format:'flatpak'},{toolchain:{command:'bad'}}),/Unsupported flatpak toolchain/);
  await assert.rejects(packageLinuxApplicationNative(app,{format:'flatpak'},{toolchain:{gpgKeyId:'short'}}),/full signing-key fingerprint/);
  await assert.rejects(packageLinuxApplicationNative(app,{format:'appimage'},{toolchain:{appimageRuntime:'/tmp/runtime'}}),/requires appimageRuntimeLicense/);
  await assert.rejects(packageLinuxApplicationNative(app,{format:'flatpak'},{toolchain:{temporaryDirectory:'/tmp/one\ntwo'}}),/Invalid native packaging toolchain/);
  await assert.rejects(exportLinuxProject(null,{distribution:{format:'flatpak',appId:'invalid'}}),/reverse-DNS/);
  await assert.rejects(exportLinuxProject(null,{distribution:{format:'zip',appId:'org.example.Test'}}),/require flatpak/);
  await assert.rejects(exportLinuxProject(null,{distribution:{format:'zip'},toolchain:{flatpak:'bad'}}),/require appimage or flatpak/);
});
test('AppImage dependency traversal rejects a library from another architecture',async t=>{
  if(process.platform!=='linux'){t.skip('Linux ELF dependency reader');return;}
  const directory=await fs.mkdtemp(path.join(os.tmpdir(),'linux-wrong-dependency-'));t.after(()=>fs.rm(directory,{recursive:true,force:true}));
  const app=await exportLinuxApplication(project),bad=await loadAppImageRuntime('arm64');
  const sysroot=path.join(directory,'target'),libraries=path.join(sysroot,'usr/lib/x86_64-linux-gnu'),appdir=path.join(directory,'AppDir');
  await fs.mkdir(libraries,{recursive:true});await fs.mkdir(appdir);await fs.writeFile(path.join(libraries,'wrong-machine'),bad.bytes);
  for(const name of app.report.dependencies)await fs.symlink('wrong-machine',path.join(libraries,name));
  const executable=path.join(directory,'program');await fs.writeFile(executable,app.executable);
  await assert.rejects(bundleAppImageDependencies({appdir,executable,compiler:app.options,toolchain:{sysroot}}),/wrong ELF architecture/);
});
