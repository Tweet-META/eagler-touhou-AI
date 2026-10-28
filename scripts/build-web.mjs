import {spawn} from 'node:child_process';
import {mkdir,copyFile,access} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const emsdk=process.argv.find(a=>a.startsWith('--emsdk='))?.slice(8)??process.env.EMSDK;
if(!emsdk)throw new Error('Provide --emsdk=/path/to/emsdk or set EMSDK');
const build=path.join(root,'build','web'),dest=path.join(root,'web','runtime');
await mkdir(build,{recursive:true});await mkdir(dest,{recursive:true});
const temporary=path.join(build,'tmp');await mkdir(temporary,{recursive:true});
const win=process.platform==='win32';
const env={...process.env,EM_CONFIG:path.join(emsdk,'.emscripten'),EMCC_TEMP_DIR:temporary,TMPDIR:temporary,TEMP:temporary,TMP:temporary};
// Use the SDK's Python directly on Windows; no batch-shell command interpolation.
let python=process.env.PYTHON??'python3';
if(win){
  const {readdir}=await import('node:fs/promises');
  const candidates=await readdir(path.join(emsdk,'python'));
  for(const name of candidates.reverse()){
    const candidate=path.join(emsdk,'python',name,'python.exe');
    try{await access(candidate);python=candidate;break;}catch{}
  }
}
function run(command,args){return new Promise((resolve,reject)=>{
  const child=spawn(command,args,{cwd:root,env,stdio:'inherit',shell:false});
  child.once('error',reject);child.once('exit',code=>code===0?resolve():reject(new Error(`Build exited ${code}`)));
});}
// Direct compilation is enough for this dependency-free module. CMake remains
// available for embedding the C++ library into a title or native executable.
await run(python,[path.join(emsdk,'upstream','emscripten','em++.py'),'-std=c++17','-O3',
  '-ffp-contract=off','-I',path.join(root,'include'),path.join(root,'src','Planner.cpp'),path.join(root,'src','WebApi.cpp'),
  '--no-entry','-sMODULARIZE=1','-sEXPORT_ES6=1','-sENVIRONMENT=web,worker,node',
  '-sFILESYSTEM=0','-sINITIAL_MEMORY=16777216','-sALLOW_MEMORY_GROWTH=0',
  "-sEXPORTED_RUNTIME_METHODS=['HEAPF32','HEAP32']",
  "-sEXPORTED_FUNCTIONS=['_ai_abi_version','_ai_create','_ai_destroy','_ai_reset','_ai_settings','_ai_begin','_ai_hazards','_ai_count','_ai_step','_ai_result']",
  '-o',path.join(build,'avoidance.mjs')]);
for(const file of ['avoidance.mjs','avoidance.wasm'])await copyFile(path.join(build,file),path.join(dest,file));
process.stdout.write(`Browser module written to ${dest}\n`);
