import {spawn} from 'node:child_process';
import {cp, copyFile, mkdir, readFile, writeFile} from 'node:fs/promises';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..');
const option = name => process.argv.find(value => value.startsWith('--' + name + '='))?.slice(name.length + 3);
const game = option('game');
if (!/^th(?:08|10|11|12|15)$/.test(game ?? '')) throw Error('Choose --game=th08|th10|th11|th12|th15');
const config = JSON.parse(await readFile(option('assets'), 'utf8'));
const assets = config.titles[game];
if (!assets?.archive || !config.fonts) throw Error('Assets configuration needs fonts and titles[game].archive');
const build = path.join(root, 'build/preview-modules', game);
const stage = path.join(build, 'overlay/ports', game + '-mp');
const common = path.join(build, 'overlay/common');
const output = path.join(root, 'web/previews', game);
const packaged = path.join(build, 'packaged');
await mkdir(packaged, {recursive:true});
const env = {...process.env, EAGLER_FONT_ROOT:config.fonts, EAGLER_RUNTIME_OUTPUT:packaged,
    EAGLER_COMMON_ROOT:common, EAGLER_COMMON_NETPLAY_ROOT:common,
    TH11_OUTPUT:path.join(build, 'web'), TH15_OUTPUT:path.join(build, 'web')};
let entry = 'portable/package-eagler.mjs';
let args = ['--multiplayer'];
if (game === 'th08' || game === 'th10') {
    const destination = path.join(stage, 'web/artifacts/multiplayer');
    await mkdir(destination, {recursive:true});
    for (const file of ['build.json', game + '-sdl.mjs', game + '-sdl.wasm'])
        await copyFile(path.join(build, 'web', file), path.join(destination, file));
}
if (game === 'th12') {
    entry = 'scripts/package-sdl.mjs';
    args = ['--output=' + packaged];
}
await new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [path.join(stage, entry), ...args], {cwd:stage, env, stdio:'inherit'});
    child.once('error', reject);
    child.once('exit', code => code === 0 ? resolve() : reject(Error('Preview packaging exited ' + code)));
});
await cp(packaged, output, {recursive:true});
await mkdir(path.join(output, 'private'), {recursive:true});
await copyFile(assets.archive, path.join(output, 'private/game.data'));
await copyFile(path.join(config.fonts, 'msgothic.ttc'), path.join(output, 'private/msgothic.ttc'));
await copyFile(path.join(config.fonts, 'unifont.otf'), path.join(output, 'private/unifont.otf'));
const music = [];
for (const item of assets.music ?? []) {
    const name = path.basename(item.file);
    await copyFile(item.file, path.join(output, 'private', name));
    music.push({path:item.path, url:'./previews/' + game + '/private/' + name});
}
await writeFile(path.join(output, 'preview.json'), JSON.stringify({game, music, localCoop:true}));
const module = game === 'th12' ? 'core' : game === 'th15' ? game : game + '-sdl';
await copyFile(path.join(output, module + '.mjs'), path.join(output, module + '-native.mjs'));
await writeFile(path.join(output, module + '.mjs'), `import create from './${module}-native.mjs';
export default async function(options) {
    const core = await create(options);
    globalThis.__eaglerAiModule = core;
    await import('../../preview-controls.js');
    globalThis.__eaglerAiControls({action:'enabled',value:true});
    return core;
}
`);
await copyFile(path.join(root, 'integrations/browser-extension/runtime-controls.js'), path.join(root, 'web/preview-controls.js'));
console.log('Prepared local human + AI preview: ' + game);
