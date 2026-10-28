import {spawn} from 'node:child_process';
import {access,mkdir,cp,copyFile,readFile,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const arg=name=>process.argv.find(a=>a.startsWith(`--${name}=`))?.slice(name.length+3);
const source=arg('source'),assets=arg('assets'),sdk=arg('emsdk')??process.env.EMSDK;
if(!source||!assets||!sdk)throw new Error('Required: --source=/eagler-touhou --assets=/TH06 --emsdk=/emsdk');
const thprac=arg('thprac')!=='off';
const unicode=arg('unifont')??process.env.EAGLER_DEVELOPMENT_UNICODE_FONT??path.join(assets,'unifont.otf');
if(thprac)await access(unicode).catch(()=>{throw new Error('THPrac needs --unifont=/path/to/unifont.otf (or --thprac=off)');});
const build=path.join(root,'build/th06'),stage=path.join(build,'source'),deps=path.join(root,'build/deps');
const binary=path.join(build,'web'),output=path.join(root,'web/th06-runtime'),temp=path.join(build,'tmp');
for(const dir of [stage,deps,binary,output,temp])await mkdir(dir,{recursive:true});
const env={...process.env,EM_CONFIG:path.join(sdk,'.emscripten'),EMCC_TEMP_DIR:temp,TEMP:temp,TMP:temp,TMPDIR:temp};
const cmake=arg('cmake')??'cmake',ninja=arg('ninja')??'ninja';
let python=process.env.PYTHON??'python3';
if(process.platform==='win32'){
  const {readdir}=await import('node:fs/promises');
  for(const dir of (await readdir(path.join(sdk,'python'))).reverse()){
    const p=path.join(sdk,'python',dir,'python.exe');try{await access(p);python=p;break;}catch{}
  }
}
function run(cmd,args){return new Promise((resolve,reject)=>{
  const p=spawn(cmd,args,{cwd:root,env,stdio:'inherit',shell:false});
  p.once('error',reject);p.once('exit',c=>c===0?resolve():reject(new Error(`Build command exited ${c}`)));
});}
const pins=[
  ['SDL','libsdl-org/SDL','f87239e71e42da91ca317a12eefb82cfbf3393eb'],
  ['SDL_image','libsdl-org/SDL_image','bec9134a26c7d0f31b36d6083c25296e04cabff5'],
  ['SDL_ttf','libsdl-org/SDL_ttf','a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b'],
  ['freetype','freetype/freetype','526ec5c47b9ebccc4754c85ac0c0cdf7c85a5e9b'],
];
for(const [name,repo,commit] of pins){
  const dest=path.join(deps,name);
  try{await access(path.join(dest,'CMakeLists.txt'));continue;}catch{}
  const response=await fetch(`https://codeload.github.com/${repo}/tar.gz/${commit}`);
  if(!response.ok)throw new Error(`Download ${name}: ${response.status}`);
  const archive=path.join(deps,`${name}.tar.gz`);await writeFile(archive,new Uint8Array(await response.arrayBuffer()));
  await mkdir(dest,{recursive:true});
  await run(python,['-c',
    'import sys,tarfile,pathlib; a,d=sys.argv[1:]; t=tarfile.open(a); members=[m for m in t.getmembers() if m.isfile() or m.isdir()]; [(setattr(m,"name",m.name.split("/",1)[1])) for m in members if "/" in m.name]; t.extractall(d,members=[m for m in members if m.name and "/" not in m.name[:1]],filter="data")',archive,dest]);
}
// Copy the selected title into ignored build output. The source checkout and
// its current branch/unfinished SDL work are never patched by this builder.
await cp(path.join(source,'ports/th06'),stage,{recursive:true,filter:entry=>{
  const rel=path.relative(path.join(source,'ports/th06'),entry).split(path.sep);
  return !rel.some(p=>['.git','build','assets','assets-ogg','vendored','tests','audit'].includes(p));
}});
for(const name of ['SDL','SDL_image','SDL_ttf'])await cp(path.join(deps,name),path.join(stage,'vendored',name),{recursive:true});
await cp(path.join(deps,'freetype'),path.join(stage,'vendored/SDL_ttf/external/freetype'),{recursive:true});
await mkdir(path.join(build,'assets'),{recursive:true});
for(const suffix of ['CM','ED','IN','MD','ST','TL'])await copyFile(path.join(assets,`紅魔郷${suffix}.DAT`),path.join(build,'assets',`紅魔郷${suffix}.DAT`));
const font=arg('font')??(process.platform==='win32'?path.join(process.env.WINDIR??'C:/Windows','Fonts/msgothic.ttc'):path.join(assets,'msgothic.ttc'));
await copyFile(font,path.join(build,'assets/msgothic.ttc'));
for(const file of ['AvoidanceHook.hpp','AvoidanceHook.cpp'])await copyFile(path.join(root,'integrations/th06',file),path.join(stage,'src',file));
const controller=path.join(stage,'src/Controller.cpp');let text=await readFile(controller,'utf8');
const needle='#else\n    return buttons;\n#endif\n}';text=text.replaceAll('\r\n','\n');
if(!text.includes(needle))throw new Error('TH06 Controller input seam changed; inspect before integrating');
text='#include "AvoidanceHook.hpp"\n'+text.replace(needle,'#else\n    return Th06Ai::filter(buttons);\n#endif\n}');
await writeFile(controller,text);
const shellFile=path.join(stage,'resources/shell.html');
let shell=await readFile(shellFile,'utf8');
if(!shell.includes('thpracEnabled: false'))throw new Error('TH06 shell option seam changed');
shell=shell.replace('thpracEnabled: false',`thpracEnabled: ${thprac}`).replace('thpracLocale: "en-US"','thpracLocale: "zh-CN"');
await writeFile(shellFile,shell);
const toCmake=p=>p.replaceAll('\\','/');
const cmakeFile=path.join(stage,'CMakeLists.txt');
await writeFile(cmakeFile,(await readFile(cmakeFile,'utf8'))+`\n# Local avoidance AI experiment\nadd_subdirectory("${toCmake(root)}" ai-core)\ntarget_sources(th06 PRIVATE src/AvoidanceHook.cpp)\ntarget_link_libraries(th06 PRIVATE eagler::ai)\n`);
await run(cmake,['-S',stage,'-B',binary,'-G','Ninja',`-DCMAKE_MAKE_PROGRAM=${ninja}`,
  `-DCMAKE_TOOLCHAIN_FILE=${path.join(sdk,'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake')}`,
  `-DEAGLER_COMMON_ROOT=${path.join(source,'common')}`,`-DTH06_ASSET_ROOT=${path.join(build,'assets')}`,
  '-DCMAKE_BUILD_TYPE=Release','-DTH_EXTERNAL_ASSETS=OFF','-DTH_WEB_SHARED_FONT=OFF','-DTH_WEB_MUSIC=BASE',
  '-DTH_ENABLE_NETPLAY=OFF','-DTH_ENABLE_MULTIPLAYER_GAMEPLAY=OFF',`-DTH_ENABLE_THPRAC=${thprac?'ON':'OFF'}`,'-DTH_ENABLE_THCRAP=OFF',
  ...(thprac?[`-DTH_UNIFONT_FILE=${path.resolve(unicode)}`]:[]),
  '-DSDL_TESTS=OFF','-DSDL_TEST_LIBRARY=OFF','-DSDLIMAGE_SAMPLES=OFF','-DSDLIMAGE_TESTS=OFF',
  '-DSDLTTF_SAMPLES=OFF','-DSDLTTF_TESTS=OFF','-DSDLTTF_PLUTOSVG=OFF']);
await run(cmake,['--build',binary,'--target','th06','--parallel',arg('jobs')??'6']);
for(const name of ['th06.html','th06.js','th06.wasm','th06.data'])await copyFile(path.join(binary,name),path.join(output,name));
await writeFile(path.join(build,'source.json'),JSON.stringify({source:path.resolve(source),dependencies:pins,assets:path.resolve(assets),thprac,gameExecuted:false},null,2));
console.log('TH06 AI ready: http://127.0.0.1:8144/th06.html');
