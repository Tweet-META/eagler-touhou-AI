import {cp, mkdir, readFile, readdir, writeFile} from 'node:fs/promises';
import path from 'node:path';

export async function packageExtension(root) {
    const source = path.join(root, 'integrations/browser-extension');
    const extension = path.join(root, 'build/browser-extension');
    const modules = path.join(extension, 'runtime/modules');
    await mkdir(modules, {recursive:true});
    await cp(source, extension, {recursive:true});
    const manifest = JSON.parse(await readFile(path.join(source, 'manifest.json'), 'utf8'));
    const resources = [];
    for (const game of (await readdir(modules)).sort()) {
        if (!/^th(?:08|10|11|12|15)$/.test(game)) continue;
        const metadata = await readFile(path.join(modules, game, 'catalog.json'), 'utf8').then(JSON.parse, () => null);
        if (!metadata) continue;
        const {module, generation} = metadata;
        const bridge = generation ? `bridge-${generation}.mjs` : 'bridge-local.mjs';
        const stem = `runtime/modules/${game}/`;
        resources.push(stem + bridge, stem + module, stem + module.replace(/\.mjs$/, '.wasm'));
    }
    if (resources.length) manifest.web_accessible_resources = [{
        resources:[...resources, 'runtime-controls.js'],
        matches:manifest.host_permissions,
    }];
    await writeFile(path.join(extension, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
}
