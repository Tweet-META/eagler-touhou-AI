import {spawn} from 'node:child_process';
import {access, copyFile, cp, mkdir, readFile, readdir, writeFile} from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {packageExtension} from './package-extension.mjs';

const root = path.resolve(import.meta.dirname, '..');
const option = name => process.argv.find(value => value.startsWith(`--${name}=`))?.slice(name.length + 3);
const source = option('source');
const sdk = option('emsdk') ?? process.env.EMSDK;
const game = option('game');
const generation = option('generation');
const preview = process.argv.includes('--preview');
if (generation && !/^[a-f0-9]{64}$/.test(generation)) throw Error('Invalid runtime generation');
if (!source || !sdk) throw Error('Required: --source=/path/to/eagler-touhou --emsdk=/path/to/emsdk');

const ports = {
    th08: {
        owner: 'web/cpp/multiplayer/RollbackDriver.cpp',
        include: '#include "../platform/BrowserRuntime.hpp"',
        input: '    return net.CaptureLocal(frame,sample);',
        patch: `    eagler::ai::online::captureTh08(sample, runtime.app.game, net.Setup().local_player, frame,
        frame != 0 && runtime.app.in_game() && !runtime.app.game.paused && !runtime.app.game.retrying &&
        !runtime.app.game.dialogue.present() && !runtime.app.game.time_stopped && !net.ReadOnly());
    return net.CaptureLocal(frame,sample);`,
        command: 'portable/build.mjs', args: ['--multiplayer'], artifact: 'th08-sdl', module: 'th08-sdl',
    },
    th10: {
        owner: 'web/cpp/platform/Application.cpp',
        include: '#include "Application.hpp"',
        input: '        if(!runtime.CaptureLocal(frame,local)){multiplayer_fail("Store local input");return false;}',
        patch: `        eagler::ai::online::captureTh10(local, *world, frame);
        if(!runtime.CaptureLocal(frame,local)){multiplayer_fail("Store local input");return false;}`,
        command: 'portable/build.mjs', args: ['--multiplayer'], artifact: 'th10-sdl', module: 'th10-sdl',
    },
    th11: {
        owner: 'web/cpp/sdl/Application.cpp',
        include: '#include <emscripten.h>',
        input: 'captured?0:motion,x,y,input)||\n       !app.netplay.Capture(app.netplay.CaptureFrame(),input)){app.error="Unable to capture TH11 multiplayer input";return false;}',
        patch: `captured?0:motion,x,y,input)){app.error="Unable to capture TH11 multiplayer input";return false;}
    eagler::ai::online::captureTh11(input, app.session, app.netplay.Setup().local_player,
      app.netplay.CaptureFrame(), !captured && !app.multiplayer_pause_requested && !(raw & 256));
    if(!app.netplay.Capture(app.netplay.CaptureFrame(),input)){app.error="Unable to capture TH11 multiplayer input";return false;}`,
        command: 'portable/build.mjs', args: ['--multiplayer'], artifact: 'th11-sdl', module: 'th11-sdl',
    },
    th12: {
        owner: 'src/multiplayer/Session.cpp',
        include: '#include <ctime>',
        input: 'if(!InputLanes::Capture(held,pause,motion,x,y,input)||!network.Capture(network.CaptureFrame(),input))return fail("TH12 input capture failed");',
        patch: `if(!InputLanes::Capture(held,pause,motion,x,y,input))return fail("TH12 input capture failed");
    eagler::ai::online::captureTh12(input, options.local, network.CaptureFrame(),
      !paused && !pause && !dialogue && !finished && fullGameFlow.status == FullGameStatus::Playing);
    if(!network.Capture(network.CaptureFrame(),input))return fail("TH12 input capture failed");`,
        command: 'scripts/build-sdl.mjs', args: ['--web'], artifact: 'touhou12', module: 'core',
    },
    th15: {
        owner: 'web/cpp/sdl/BrowserRuntime.cpp',
        include: '#include "../multiplayer/InputLanes.hpp"',
        input: '||!network.Capture(network.CaptureFrame(),sampled)){last_error=network.Error();return false;}}',
        patch: `){last_error=network.Error();return false;}
  auto* stage = app->scene();
  if(stage)eagler::ai::online::captureTh15(sampled, stage->battle, app->animations, network.Setup().local_player,
    network.CaptureFrame(), unsigned(app->progress.stage),
    !network.ReadOnly() && !(held&0x100) && !stage->messages.active() &&
    (!app->pause || app->pause->state.screen == PauseScreen::Inactive));
  if(!network.Capture(network.CaptureFrame(),sampled)){last_error=network.Error();return false;}}`,
        command: 'web/scripts/build-sdl-application.mjs', args: ['--release', '--multiplayer'], artifact: 'th15', module: 'th15',
    },
};
const port = ports[game];
if (!port) throw Error(`Unsupported module runtime: ${game}`);

let common = path.resolve(source, 'common');
const portName = `${game}-mp`;
const donor = path.resolve(source, 'ports', portName);
const work = path.join(root, 'build', preview ? 'preview-modules' : 'online-modules');
const build = path.join(work, game);
const overlay = path.join(build, 'overlay');
const stage = path.join(overlay, 'ports', portName);
const output = path.join(build, 'web');
const temp = path.join(build, 'tmp');
const extension = path.join(root, 'build', 'browser-extension');
const runtime = preview ? path.join(root, 'web', 'previews', game) : path.join(extension, 'runtime', 'modules', game);
for (const dir of [stage, output, temp, runtime]) await mkdir(dir, {recursive:true});

const excluded = new Set(['.git', 'node_modules', 'artifacts', 'decompiled', 'build-eagler-multiplayer', 'assets']);
await cp(donor, stage, {recursive:true, filter:entry =>
    !path.relative(donor, entry).split(path.sep).some(segment => excluded.has(segment))});
await cp(path.resolve(source, 'tools/build'), path.join(overlay, 'tools/build'), {recursive:true});

const ownerPath = path.join(stage, port.owner);
const ownerDir = path.dirname(ownerPath);
const owner = (await readFile(ownerPath, 'utf8')).replaceAll('\r\n', '\n');
function replaceOnce(text, from, to) {
    if (text.split(from).length !== 2) throw Error(`${game}: native capture seam changed (${from})`);
    return text.replace(from, to);
}
if (game === 'th11') {
    const revision = path.join(stage, 'portable/common-revision.mjs');
    const manifest = pathToFileURL(path.resolve(source, 'launcher/lib/release-manifest.mjs')).href;
    await writeFile(revision, replaceOnce(await readFile(revision, 'utf8'),
        '../../../launcher/lib/release-manifest.mjs', manifest));
}
if (game === 'th15') {
    const directory = path.join(stage, 'web/cpp/game');
    const declaration = path.join(directory, 'LaserScene.hpp');
    const implementation = path.join(directory, 'LaserScene.cpp');
    const seam = 'MovingLaserMotion* moving_motion(u32 id)noexcept;';
    await writeFile(declaration, replaceOnce(await readFile(declaration, 'utf8'), seam,
        seam + '\n    bool ai_moving_full_length(const LaserObject& laser) const noexcept;'));
    const method = 'MovingLaserMotion* LaserScene::moving_motion(u32 id)noexcept';
    await writeFile(implementation, replaceOnce(await readFile(implementation, 'utf8'), method,
        `bool LaserScene::ai_moving_full_length(const LaserObject& laser) const noexcept {
    return laser.kind == LaserKind::moving && (static_cast<const MovingLaser&>(laser).request.transform_parameter & 2);
}
${method}`));
}
const hooked = replaceOnce(owner, port.include, `${port.include}\n#include "EaglerAiAdapter.hpp"\n#include "EaglerAiPlanner.inc"`);
let patch = port.patch;
if (preview) {
    common = path.join(overlay, 'common');
    await cp(path.resolve(source, 'common'), common, {recursive:true,
        filter:entry=>!path.relative(path.resolve(source, 'common'),entry).split(path.sep).some(s=>['.git','node_modules','artifacts'].includes(s))});
    const gateFile = path.join(common, 'src/netplay/NetplaySession.cpp');
    await writeFile(gateFile, replaceOnce(await readFile(gateFile, 'utf8'),
        '    configured_ = true;', '    configured_ = true;\n    peerHello_.fill(true);\n    peerReady_.fill(true);\n    localReady_ = true;'));
    if (game === 'th11') {
        const file = path.join(stage, 'portable/common-revision.mjs');
        await writeFile(file, (await readFile(file,'utf8')).replace(
            "process.env.EAGLER_COMMON_ROOT || resolve(import.meta.dirname, '../../../common')",
            JSON.stringify(path.resolve(source, 'common'))));
    }
    const nativeDirectory = path.join(stage, game === 'th12' ? 'src/multiplayer' : 'web/cpp/multiplayer');
    const connection = path.join(nativeDirectory, game === 'th08' ? 'NetworkConnection.cpp' : 'NetplayRuntime.cpp');
    const text = await readFile(connection, 'utf8');
    const connect = /bool (?:NetworkConnection|NetplayRuntime)::Connect\(const char\* \w+\)\s*\{/;
    if (!connect.test(text)) throw Error(game + ': local connection seam changed');
    await writeFile(connection, text.replace(connect, match=>match+'\n    return '+(game==='th08'?'net.Configured()':'configured_')+';'));
    const variable = {th08:'sample',th10:'local',th11:'input',th12:'input',th15:'sampled'}[game];
    const network = {th08:'net',th10:'runtime',th11:'app.netplay',th12:'network',th15:'network'}[game];
    const frame = {th08:'frame',th10:'frame',th11:'app.netplay.CaptureFrame()',th12:'network.CaptureFrame()',th15:'network.CaptureFrame()'}[game];
    const call = patch.match(/eagler::ai::online::captureTh\d+\([\s\S]*?;/)?.[0];
    if (!call) throw Error(game+': AI call not found');
    let ai = call.replace(new RegExp('\\b'+variable+'\\b','g'), 'aiInput')
        .replace('net.Setup().local_player','1').replace('app.netplay.Setup().local_player','1')
        .replace('options.local','1').replace('network.Setup().local_player','1');
    if (game === 'th10') ai = ai.replace('aiInput, *world, frame)', 'aiInput, *world, frame, 1)');
    if (game === 'th15') patch = patch.replace('if(stage)'+call, call);
    patch = patch.replace(call, `{ Netplay::FrameInput aiInput;
      ${game === 'th15' ? 'if(stage)' : ''}${ai}
      ${network}.SubmitRemote(1, ${frame}, aiInput);
    }`);
}
await writeFile(ownerPath, replaceOnce(hooked, port.input, patch));
await copyFile(path.join(root, 'include/eagler/ai/Planner.hpp'), path.join(ownerDir, 'Planner.hpp'));
await copyFile(path.join(root, 'include/eagler/ai/InputDelay.hpp'), path.join(ownerDir, 'InputDelay.hpp'));
await copyFile(path.join(root, 'integrations/online/Seat.hpp'), path.join(ownerDir, 'Seat.hpp'));
await copyFile(path.join(root, 'integrations/online', `${game}.hpp`), path.join(ownerDir, 'EaglerAiAdapter.hpp'));
await writeFile(path.join(ownerDir, 'EaglerAiPlanner.inc'),
    (await readFile(path.join(root, 'src/Planner.cpp'), 'utf8')).replace('<eagler/ai/Planner.hpp>', '"Planner.hpp"'));

let python = process.env.TH_PYTHON ?? process.env.PYTHON ?? 'python3';
if (process.platform === 'win32') {
    for (const entry of (await readdir(path.join(sdk, 'python'))).reverse()) {
        const candidate = path.join(sdk, 'python', entry, 'python.exe');
        if (await access(candidate).then(() => true, () => false)) { python = candidate; break; }
    }
}
const env = {...process.env, EMSDK:path.resolve(sdk), TH15_EMSDK:path.resolve(sdk),
    EM_CONFIG:path.resolve(sdk, '.emscripten'), EAGLER_COMMON_ROOT:common, EAGLER_COMMON_NETPLAY_ROOT:common,
    TH_PYTHON:python, EMSDK_PYTHON:python, TH_BUILD_JOBS:option('jobs') ?? '4',
    EMCC_TEMP_DIR:temp, TEMP:temp, TMP:temp, TMPDIR:temp,
    TH08_MP_BUILD_ROOT:output, TH10_OUTPUT:output, TH11_OUTPUT:output, TH15_OUTPUT:output,
    TH12_SDL3_SOURCE:path.join(root,'build/deps/SDL'), TH12_FREETYPE_SOURCE:path.join(root,'build/deps/freetype')};
const gitConfigSlot = Number(env.GIT_CONFIG_COUNT ?? 0);
env.GIT_CONFIG_COUNT = String(gitConfigSlot + 1);
env[`GIT_CONFIG_KEY_${gitConfigSlot}`] = 'safe.directory';
env[`GIT_CONFIG_VALUE_${gitConfigSlot}`] = path.resolve(source).replaceAll('\\', '/');
const args = [...port.args, ...(game === 'th12' ? [`--build-dir=${output}`] : [])];
await new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [path.join(stage, port.command), ...args], {cwd:stage, env, stdio:'inherit'});
    child.once('error', reject);
    child.once('exit', code => code === 0 ? resolve() : reject(Error(`${game} build exited ${code}`)));
});

const loader = path.join(output, `${port.artifact}.mjs`);
const wasmFile = path.join(output, `${port.artifact}.wasm`);
const glue = await readFile(loader, 'utf8');
const exports = WebAssembly.Module.exports(new WebAssembly.Module(await readFile(wasmFile)));
for (const symbol of ['enable', 'configure', 'metric', 'team_mode', 'follow_attack', 'auto_bomb',
    'action_duration', 'input_delay', 'switch_weight', 'objectives', 'edges', 'danger', 'density']) {
    const name = `eagler_ai_${symbol}`;
    const minified = glue.match(new RegExp(`Module\\["_${name}"\\]=wasmExports\\["([A-Za-z0-9_$]+)"\\]`))?.[1];
    if (!glue.includes(`_${name}`) || !exports.some(entry => entry.name === (minified ?? name) && entry.kind === 'function'))
        throw Error(`${game}: missing native AI export ${name}`);
}
await copyFile(loader, path.join(runtime, `${port.module}.mjs`));
await copyFile(wasmFile, path.join(runtime, `${port.module}.wasm`));
const bridge = generation ? `bridge-${generation}.mjs` : 'bridge-local.mjs';
await writeFile(path.join(runtime, bridge), `import create from './${port.module}.mjs';
const wasm = new URL('./${port.module}.wasm', import.meta.url).href;
export default async function createAiRuntime(options = {}) {
    const originalInstantiate = options.instantiateWasm;
    const originalLocate = options.locateFile;
    const locateFile = (file, prefix) => file.endsWith('.wasm') ? wasm :
        originalLocate ? originalLocate(file, prefix) : prefix + file;
    const instantiateWasm = originalInstantiate && function (imports, ready) {
        // The host callback captures the raw exports in a closure. Let it
        // run as usual, redirecting only its immediate request for this Wasm.
        const fetch = globalThis.fetch;
        globalThis.fetch = function (input, init) {
            const requested = new URL(input instanceof Request ? input.url : input, location.href);
            const stock = new URL('./${port.module}.wasm', location.href);
            return fetch.call(globalThis, requested.origin === stock.origin && requested.pathname === stock.pathname ? wasm : input, init);
        };
        try { return originalInstantiate.call(this, imports, ready); }
        finally { globalThis.fetch = fetch; }
    };
    const runtime = await create({...options, locateFile, ...(instantiateWasm ? {instantiateWasm} : {})});
    globalThis.__eaglerAiModule = runtime;
    await import('../../../runtime-controls.js');
    return runtime;
}
`);
await writeFile(path.join(runtime, 'catalog.json'), JSON.stringify({game, module:`${port.module}.mjs`, generation}));
if (!preview) await packageExtension(root);
