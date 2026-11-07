import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const directory=process.argv[2]??path.join(root,'build/browser-extension/runtime');
const [code,binary]=await Promise.all([
  readFile(path.join(directory,'th06.js'),'utf8'),
  readFile(path.join(directory,'th06.wasm')),
]);
let context;
let finish,fail;
const loaded=new Promise((resolve,reject)=>{finish=resolve;fail=reject;});
const timeout=setTimeout(()=>fail(Error('AI runtime loader timed out')),10000);
const unexpectedFetch=url=>{
  const error=Error(`AI runtime attempted to download a different Wasm: ${url}`);
  fail(error);throw error;
};
context=vm.createContext({
  console,URL,TextDecoder,TextEncoder,setTimeout,clearTimeout,
  window:{},document:{currentScript:{src:'https://touhouonline.com/runtime/th06/multiplayer/th06.js'}},
  navigator:{},fetch:unexpectedFetch,
  Module:{
    wasmBinary:new Uint8Array(binary),noInitialRun:true,
    // Hold startup before constructors, assets and the game loop. The real
    // generated loader must still instantiate the entire paired Wasm first.
    preRun:[()=>{context.addRunDependency('runtime-loader-check');finish();}],
    onAbort:reason=>fail(Error(String(reason))),
  },
  WebAssembly:Object.assign(Object.create(WebAssembly),{
    instantiate:async(bytes,imports)=>{
      assert.deepEqual(Buffer.from(bytes),binary,'The loader must use the bundled AI Wasm');
      return WebAssembly.instantiate(bytes,imports);
    },
    instantiateStreaming:()=>unexpectedFetch('streaming compilation'),
  }),
});
try{
  const promiseEntry='createWasm().then(()=>run());';
  const callbackEntry='createWasm();run();';
  assert.ok(code.includes(promiseEntry)||code.includes(callbackEntry),
    'Unrecognized runtime startup; update the loader check');
  // Emscripten 4 schedules run through its dependency callbacks. Emscripten 6
  // exposes the startup promise here, so forward its rejection to the check.
  const loader=code.replace(promiseEntry,'createWasm().then(()=>run()).catch(Module.onAbort);');
  vm.runInContext(loader,context,
    {filename:'th06.js',timeout:10000});
  await loaded;
  assert.equal(typeof context.Module._th06_ai_metric,'function','AI exports must be present');
  console.log('TH06 AI loader passed: bundled Wasm instantiated; no network fetch; AI exports present.');
}finally{
  clearTimeout(timeout);
}
