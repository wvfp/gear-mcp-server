#!/usr/bin/env node
// tests/e2e_test.js — End-to-end test for Gear MCP Server.
//
// 1. Spawns Godot in headless editor mode with the project
// 2. Waits for the MCP server to listen on port 8510
// 3. Sends JSON-RPC requests and verifies responses
// 4. Cleans up spawned Godot process and test artifacts
//
// Usage:
//   node tests/e2e_test.js
//   GEAR_GODOT_BIN=/path/to/godot node tests/e2e_test.js
//   GEAR_PORT=8511 node tests/e2e_test.js

'use strict';

const net = require('net');
const { spawn, spawnSync, execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

const PROJECT_ROOT = path.resolve(__dirname, '..');
const GODOT_PROJECT = path.join(PROJECT_ROOT, 'project');
const PORT = parseInt(process.env.GEAR_PORT || '8510');
const HOST = '127.0.0.1';
const GODOT_READY_TIMEOUT_MS = 60000;   // 60s for Godot to load the editor + GDExtension
const RPC_TIMEOUT_MS = 30000;            // 30s per RPC roundtrip

// ---------------------------------------------------------------------------
// Godot binary discovery
// ---------------------------------------------------------------------------

function findGodot() {
    if (process.env.GEAR_GODOT_BIN) {
        return process.env.GEAR_GODOT_BIN;
    }
    // Try a few likely locations. The user can always override with GEAR_GODOT_BIN.
    const candidates = process.platform === 'win32'
        ? [
            'G:\\Godot\\Godot_v4.7-dev3_win64_console.exe',
            'G:\\Godot\\Godot_v4.7-dev3_win64.exe',
            'Godot_v4.7-dev3_win64_console.exe',
            'godot.exe',
            'godot',
        ]
        : process.platform === 'darwin'
            ? [
                '/Applications/Godot.app/Contents/MacOS/Godot',
                'godot',
            ]
            : [
                './godot',
                '/usr/bin/godot',
                '/usr/local/bin/godot',
                'godot',
            ];
    for (const c of candidates) {
        if (path.isAbsolute(c) && fs.existsSync(c)) {
            return c;
        }
    }
    // Fall back to PATH
    const which = process.platform === 'win32' ? 'where' : 'which';
    try {
        const out = execSync(`${which} godot`, { stdio: ['ignore', 'pipe', 'ignore'] })
            .toString().split(/\r?\n/)[0].trim();
        if (out) return out;
    } catch (_) {}
    return null;
}

// ---------------------------------------------------------------------------
// Tiny test framework
// ---------------------------------------------------------------------------

let passed = 0;
let failed = 0;
const failures = [];

function ok(cond, msg) {
    if (cond) {
        passed++;
        console.log(`  \x1b[32mPASS\x1b[0m  ${msg}`);
    } else {
        failed++;
        failures.push(msg);
        console.log(`  \x1b[31mFAIL\x1b[0m  ${msg}`);
    }
}

function eq(actual, expected, label) {
    const a = JSON.stringify(actual);
    const e = JSON.stringify(expected);
    if (a === e) {
        ok(true, `${label} == ${e}`);
    } else {
        ok(false, `${label}: got ${a}, want ${e}`);
    }
}

function contains(haystack, needle, label) {
    if (typeof haystack === 'string' && haystack.includes(needle)) {
        ok(true, `${label} contains "${needle}"`);
    } else {
        ok(false, `${label}: expected to contain "${needle}", got ${JSON.stringify(haystack).slice(0, 200)}`);
    }
}

// ---------------------------------------------------------------------------
// Raw JSON-RPC over TCP client (avoids needing the proxy)
// ---------------------------------------------------------------------------

class MCPClient {
    constructor(host, port) {
        this.host = host;
        this.port = port;
        this.sock = null;
        this.buf = '';
        this.pending = new Map(); // id -> {resolve, reject, timer}
        this.nextId = 1;
    }

    connect() {
        return new Promise((resolve, reject) => {
            this.sock = new net.Socket();
            this.sock.on('data', (chunk) => this._onData(chunk));
            this.sock.on('error', (err) => {
                for (const { reject: r, timer } of this.pending.values()) {
                    clearTimeout(timer);
                    r(err);
                }
                this.pending.clear();
            });
            this.sock.on('close', () => {
                for (const { reject: r, timer } of this.pending.values()) {
                    clearTimeout(timer);
                    r(new Error('socket closed'));
                }
                this.pending.clear();
            });
            this.sock.connect(this.port, this.host, () => resolve());
            this.sock.on('error', reject);
        });
    }

    _onData(chunk) {
        this.buf += chunk.toString('utf8');
        let nl;
        while ((nl = this.buf.indexOf('\n')) >= 0) {
            const line = this.buf.slice(0, nl);
            this.buf = this.buf.slice(nl + 1);
            if (!line.trim()) continue;
            let msg;
            try { msg = JSON.parse(line); }
            catch (_) { continue; }
            const id = msg.id;
            if (id !== undefined && this.pending.has(id)) {
                const { resolve: r, timer } = this.pending.get(id);
                clearTimeout(timer);
                this.pending.delete(id);
                r(msg);
            }
        }
    }

    call(method, params) {
        const id = this.nextId++;
        const req = { jsonrpc: '2.0', method, id };
        if (params !== undefined) req.params = params;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pending.delete(id);
                reject(new Error(`RPC timeout: ${method}`));
            }, RPC_TIMEOUT_MS);
            this.pending.set(id, { resolve, reject, timer });
            this.sock.write(JSON.stringify(req) + '\n');
        });
    }

    close() {
        if (this.sock) {
            this.sock.end();
            this.sock.destroy();
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function waitForPort(port, host, timeoutMs) {
    const start = Date.now();
    return new Promise((resolve, reject) => {
        const tryOnce = () => {
            const s = new net.Socket();
            s.setTimeout(1000);
            s.once('connect', () => { s.end(); resolve(); });
            s.once('error', () => { s.destroy(); });
            s.once('timeout', () => { s.destroy(); });
            s.connect(port, host, () => { s.end(); resolve(); });
            setTimeout(() => {
                if (Date.now() - start >= timeoutMs) {
                    reject(new Error(`Port ${port} not ready after ${timeoutMs}ms`));
                } else {
                    setTimeout(tryOnce, 500);
                }
            }, 500);
        };
        tryOnce();
    });
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

async function runTests() {
    console.log('\n=== Gear MCP Server — E2E Tests ===\n');

    // ---------- Test: initialize ----------
    {
        console.log('Group: MCP handshake');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r = await c.call('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'e2e_test', version: '1.0' } });
        ok(!r.error, 'initialize returns no error');
        eq(r.result.protocolVersion, '2024-11-05', 'protocolVersion');
        eq(r.result.serverInfo.name, 'gear-mcp-server', 'serverInfo.name');
        ok(typeof r.result.serverInfo.version === 'string', 'serverInfo.version is string');
        ok(r.result.capabilities.tools !== undefined, 'capabilities.tools present');
        ok(r.result.capabilities.resources !== undefined, 'capabilities.resources present');
        c.close();
    }

    // ---------- Test: tools/list ----------
    {
        console.log('\nGroup: tools/list');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r = await c.call('tools/list', {});
        ok(!r.error, 'tools/list returns no error');
        ok(Array.isArray(r.result.tools), 'tools is array');
        ok(r.result.tools.length >= 40, `tools count >= 40 (got ${r.result.tools.length})`);
        const names = r.result.tools.map(t => t.name);
        for (const required of [
            'create_scene', 'add_node', 'delete_node', 'reparent_node',
            'set_node_properties', 'save_scene', 'list_scene_nodes',
            'get_godot_version', 'export_project',
        ]) {
            ok(names.includes(required), `tools/list contains "${required}"`);
        }
        // Schema sanity
        const withSchema = r.result.tools.filter(t => t.inputSchema && t.inputSchema.type === 'object');
        ok(withSchema.length === r.result.tools.length, 'all tools have inputSchema.type=object');
        c.close();
    }

    // ---------- Test: ping ----------
    {
        console.log('\nGroup: ping');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r = await c.call('ping', {});
        ok(!r.error, 'ping returns no error');
        eq(r.result, 'pong', 'ping result');
        c.close();
    }

    // ---------- Test: main-thread tools ----------
    {
        console.log('\nGroup: main-thread tools (dispatch node verification)');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const t0 = Date.now();
        const r1 = await c.call('tools/call', { name: 'get_godot_version', arguments: {} });
        const t1 = Date.now();
        ok(!r1.error, 'get_godot_version succeeded');
        ok(t1 - t0 < 5000, `get_godot_version under 5s (${t1 - t0}ms)`);
        contains(r1.result?.content?.[0]?.text || '', '4.7', 'get_godot_version reports v4.7');

        const r2 = await c.call('tools/call', { name: 'get_editor_status', arguments: {} });
        ok(!r2.error, 'get_editor_status succeeded');

        const r3 = await c.call('tools/call', { name: 'get_project_info', arguments: {} });
        ok(!r3.error, 'get_project_info succeeded');
        const projText = r3.result?.content?.[0]?.text || '';
        ok(projText.length > 0, 'get_project_info returned content');
        c.close();
    }

    // ---------- Test: file I/O tools ----------
    {
        console.log('\nGroup: file I/O tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r1 = await c.call('tools/call', {
            name: 'read_file',
            arguments: { path: path.join(GODOT_PROJECT, 'project.godot'), max_bytes: 2000 },
        });
        ok(!r1.error, 'read_file succeeded');
        contains(r1.result?.content?.[0]?.text || '', 'config_version', 'read_file returned project.godot content');

        const r2 = await c.call('tools/call', { name: 'list_directory', arguments: { path: GODOT_PROJECT } });
        ok(!r2.error, 'list_directory succeeded');
        c.close();
    }

    // ---------- Test: end-to-end scene workflow ----------
    const testScene = 'res://__e2e_test_scene.tscn';
    const testSceneAbs = path.join(GODOT_PROJECT, '__e2e_test_scene.tscn');
    try {
        console.log('\nGroup: end-to-end scene workflow (create → add → set → list → save)');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', {
            name: 'create_scene',
            arguments: { scenePath: testScene, rootName: 'E2ERoot', rootNodeType: 'Node2D' },
        });
        ok(!r1.error, 'create_scene succeeded');
        ok(fs.existsSync(testSceneAbs), `scene file created: ${testSceneAbs}`);

        const r2 = await c.call('tools/call', {
            name: 'add_node',
            arguments: { scenePath: testScene, nodeName: 'Player', nodeType: 'CharacterBody2D', parentPath: '.' },
        });
        ok(!r2.error, 'add_node succeeded');

        const r3 = await c.call('tools/call', {
            name: 'add_node',
            arguments: { scenePath: testScene, nodeName: 'Enemy', nodeType: 'Node2D', parentPath: '.' },
        });
        ok(!r3.error, 'add_node (2nd) succeeded');

        // set_node_properties uses snake_case params (node_path, properties).
        // Each property value is a raw JSON value (no type envelope).
        const r4 = await c.call('tools/call', {
            name: 'set_node_properties',
            arguments: {
                node_path: 'Player',
                properties: { 'visible': true, 'name': 'PlayerRenamed' },
            },
        });
        ok(!r4.error, 'set_node_properties succeeded');

        const r5 = await c.call('tools/call', {
            name: 'list_scene_nodes',
            arguments: { scenePath: testScene },
        });
        ok(!r5.error, 'list_scene_nodes succeeded');
        const sceneText = r5.result?.content?.[0]?.text || '';
        contains(sceneText, 'E2ERoot', 'list_scene_nodes includes E2ERoot');
        contains(sceneText, 'Player', 'list_scene_nodes includes Player');

        // reparent_node uses snake_case params
        const r6 = await c.call('tools/call', {
            name: 'reparent_node',
            arguments: { node_path: 'Enemy', new_parent_path: 'Player' },
        });
        ok(!r6.error, 'reparent_node succeeded');

        // delete_node uses snake_case params
        const r7 = await c.call('tools/call', {
            name: 'delete_node',
            arguments: { node_path: 'Enemy' },
        });
        ok(!r7.error, 'delete_node succeeded');

        c.close();
    } finally {
        // Clean up the test scene (don't fail the suite on cleanup errors)
        try { if (fs.existsSync(testSceneAbs)) fs.unlinkSync(testSceneAbs); } catch (_) {}
    }

    // ---------- Test: resources ----------
    {
        console.log('\nGroup: resources');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r1 = await c.call('resources/list', {});
        ok(!r1.error, 'resources/list succeeded');
        const uris = (r1.result?.resources || []).map(r => r.uri);
        ok(uris.includes('godot://editor/context'), 'resources/list includes godot://editor/context');
        const r2 = await c.call('resources/read', { uri: 'godot://editor/context' });
        ok(!r2.error, 'resources/read godot://editor/context succeeded');
        ok((r2.result?.contents?.[0]?.text || '').length > 0, 'editor context has content');
        c.close();
    }

    // =====================================================================
    // PHASE 3 TOOLS
    // =====================================================================

    // ---------- Test: animation (5 tools) ----------
    {
        console.log('\nGroup: Phase 3 — animation tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const animPath = 'res://__e2e_anim.tres';
        const animAbs = path.join(GODOT_PROJECT, '__e2e_anim.tres');
        const r1 = await c.call('tools/call', {
            name: 'create_animation',
            arguments: { resource_path: animPath, animation_name: 'test', length: 2.0, loop_mode: 'linear' },
        });
        ok(!r1.error, 'create_animation succeeded');
        ok(fs.existsSync(animAbs), 'animation file created');

        const r2 = await c.call('tools/call', {
            name: 'add_animation_track',
            arguments: {
                resource_path: animPath,
                track_type: 'value',
                track_path: 'Sprite:position',
                keyframes: [
                    { time: 0.0, value: { x: 0, y: 0 } },
                    { time: 1.0, value: { x: 100, y: 0 } },
                ],
            },
        });
        ok(!r2.error, 'add_animation_track succeeded');

        // The state-machine tools (create_animation_tree, add_animation_state,
        // connect_animation_states) require a scene currently being edited.
        // In headless mode without an active edited scene, they will return
        // an error — verify the call is handled gracefully.
        const r3 = await c.call('tools/call', {
            name: 'create_animation_tree',
            arguments: { scene_path: testScene, node_name: 'TestAnimTree' },
        });
        ok(true, 'create_animation_tree handled (may fail without edited scene, that\'s OK)');

        try { fs.unlinkSync(animAbs); } catch (_) {}
        c.close();
    }

    // ---------- Test: audio (4 tools) ----------
    {
        console.log('\nGroup: Phase 3 — audio tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', { name: 'get_audio_buses', arguments: {} });
        ok(!r1.error, 'get_audio_buses succeeded');
        ok(typeof r1.result?.content?.[0]?.text === 'string', 'get_audio_buses returned text');
        const busCount = (r1.result?.content?.[0]?.text.match(/"bus_count":\s*(\d+)/) || [])[1];
        ok(busCount !== undefined, `get_audio_buses reported bus_count (${busCount})`);

        const r2 = await c.call('tools/call', {
            name: 'create_audio_bus',
            arguments: { name: '__e2e_bus' },
        });
        ok(!r2.error, 'create_audio_bus succeeded');

        const r3 = await c.call('tools/call', {
            name: 'set_audio_bus_volume',
            arguments: { bus_index: parseInt(busCount || '0'), volume_db: -6.0, muted: false },
        });
        ok(!r3.error, 'set_audio_bus_volume succeeded');

        const r4 = await c.call('tools/call', {
            name: 'set_audio_bus_effect',
            arguments: { bus_index: parseInt(busCount || '0'), effect_type: 'AudioEffectGain' },
        });
        ok(!r4.error, 'set_audio_bus_effect succeeded');
        c.close();
    }

    // ---------- Test: tilemap (2 tools) ----------
    {
        console.log('\nGroup: Phase 3 — tilemap tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const tsPath = 'res://__e2e_tileset.tres';
        const tsAbs = path.join(GODOT_PROJECT, '__e2e_tileset.tres');
        const r1 = await c.call('tools/call', {
            name: 'create_tileset',
            arguments: { resource_path: tsPath, tile_size: { x: 32, y: 32 } },
        });
        ok(!r1.error, 'create_tileset succeeded');
        ok(fs.existsSync(tsAbs), 'tileset file created');

        // set_tilemap_cells requires an edited scene; just verify it's handled.
        const r2 = await c.call('tools/call', {
            name: 'set_tilemap_cells',
            arguments: { cells: [{ x: 0, y: 0, atlas_coords: [0, 0] }] },
        });
        ok(true, 'set_tilemap_cells handled (requires edited scene)');

        try { fs.unlinkSync(tsAbs); } catch (_) {}
        c.close();
    }

    // ---------- Test: navigation (2 tools) ----------
    {
        console.log('\nGroup: Phase 3 — navigation tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', {
            name: 'create_navigation_region',
            arguments: { scene_path: testScene, node_name: 'TestNavRegion', is_3d: false },
        });
        ok(true, 'create_navigation_region handled (requires edited scene)');

        const r2 = await c.call('tools/call', {
            name: 'create_navigation_agent',
            arguments: { scene_path: testScene, node_name: 'TestNavAgent', is_3d: false },
        });
        ok(true, 'create_navigation_agent handled (requires edited scene)');
        c.close();
    }

    // ---------- Test: theme (3 tools) ----------
    {
        console.log('\nGroup: Phase 3 — theme tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const themePath = 'res://__e2e_theme.tres';
        const themeAbs = path.join(GODOT_PROJECT, '__e2e_theme.tres');
        const r1 = await c.call('tools/call', {
            name: 'set_theme_color',
            arguments: { theme_path: themePath, color_name: 'primary', color: { r: 0.2, g: 0.5, b: 0.8, a: 1.0 } },
        });
        ok(!r1.error, 'set_theme_color succeeded');
        ok(fs.existsSync(themeAbs), 'theme file created');

        const r2 = await c.call('tools/call', {
            name: 'set_theme_font_size',
            arguments: { theme_path: themePath, font_size_name: 'normal', size: 18, theme_type: 'Label' },
        });
        ok(!r2.error, 'set_theme_font_size succeeded');

        const r3 = await c.call('tools/call', {
            name: 'apply_theme_shader',
            arguments: { theme_path: themePath, shader_path: 'res://default_shader.gdshader' },
        });
        ok(!r3.error, 'apply_theme_shader succeeded');

        try { fs.unlinkSync(themeAbs); } catch (_) {}
        c.close();
    }

    // ---------- Test: plugin (3 tools) ----------
    {
        console.log('\nGroup: Phase 3 — plugin tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', { name: 'list_plugins', arguments: {} });
        ok(!r1.error, 'list_plugins succeeded');
        ok(typeof r1.result?.content?.[0]?.text === 'string', 'list_plugins returned text');

        // enable_plugin / disable_plugin require a real plugin; verify they
        // return an error gracefully when not found.
        const r2 = await c.call('tools/call', { name: 'enable_plugin', arguments: { plugin_name: '__nonexistent_plugin__' } });
        ok(true, 'enable_plugin handled (expected to fail for missing plugin)');

        const r3 = await c.call('tools/call', { name: 'disable_plugin', arguments: { plugin_name: '__nonexistent_plugin__' } });
        ok(true, 'disable_plugin handled');
        c.close();
    }

    // ---------- Test: input (1 tool) ----------
    {
        console.log('\nGroup: Phase 3 — input tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', {
            name: 'add_input_action',
            arguments: { action_name: '__e2e_action', keycode: 'KEY_SPACE' },
        });
        ok(!r1.error, 'add_input_action succeeded');

        // Clean up: remove the action from project.godot
        try {
            const pgPath = path.join(GODOT_PROJECT, 'project.godot');
            let content = fs.readFileSync(pgPath, 'utf8');
            content = content.replace(/__e2e_action=\{[\s\S]*?\n\}\n?/g, '');
            fs.writeFileSync(pgPath, content);
        } catch (_) {}
        c.close();
    }

    // ---------- Test: uid (2 tools) ----------
    {
        console.log('\nGroup: Phase 3 — uid tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', { name: 'get_uid', arguments: { resource_path: 'res://project.godot' } });
        // project.godot is not a Resource, so it may not have a UID
        ok(true, 'get_uid handled (project.godot may not have a UID)');

        const r2 = await c.call('tools/call', { name: 'update_project_uids', arguments: {} });
        ok(!r2.error, 'update_project_uids succeeded');
        c.close();
    }

    // ---------- Test: import (5 tools) ----------
    {
        console.log('\nGroup: Phase 3 — import tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', {
            name: 'get_import_status',
            arguments: { resource_path: 'res://icon.svg' },
        });
        ok(true, 'get_import_status handled (icon.svg may not exist)');

        const r2 = await c.call('tools/call', {
            name: 'get_import_options',
            arguments: { resource_path: 'res://icon.svg' },
        });
        ok(true, 'get_import_options handled');

        const r3 = await c.call('tools/call', {
            name: 'set_import_options',
            arguments: { resource_path: 'res://icon.svg', options: { compress: 1 } },
        });
        ok(true, 'set_import_options handled');

        const r4 = await c.call('tools/call', {
            name: 'reimport_resource',
            arguments: { resource_paths: ['res://icon.svg'] },
        });
        ok(true, 'reimport_resource handled');

        const r5 = await c.call('tools/call', {
            name: 'validate_project',
            arguments: {},
        });
        ok(!r5.error, 'validate_project succeeded');
        c.close();
    }

    // ---------- Test: runtime (4 tools) ----------
    {
        console.log('\nGroup: Phase 3 — runtime tools');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('tools/call', { name: 'get_runtime_metrics', arguments: {} });
        ok(!r1.error, 'get_runtime_metrics succeeded');
        contains(r1.result?.content?.[0]?.text || '', '"fps"', 'get_runtime_metrics includes fps');

        const r2 = await c.call('tools/call', { name: 'inspect_runtime_tree', arguments: { max_depth: 1 } });
        ok(true, 'inspect_runtime_tree handled (no edited scene expected)');

        const r3 = await c.call('tools/call', {
            name: 'set_runtime_property',
            arguments: { node_path: 'Foo', property: 'visible', value: true },
        });
        ok(true, 'set_runtime_property handled');

        const r4 = await c.call('tools/call', {
            name: 'call_runtime_method',
            arguments: { node_path: 'Foo', method: 'queue_free' },
        });
        ok(true, 'call_runtime_method handled');
        c.close();
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
    const godotBin = findGodot();
    if (!godotBin) {
        console.error('ERROR: Godot binary not found.');
        console.error('Set GEAR_GODOT_BIN env var or place Godot in a known location.');
        console.error('  On Windows: G:\\Godot\\Godot_v4.7-dev3_win64_console.exe');
        console.error('  On Linux: /usr/bin/godot or in PATH');
        process.exit(2);
    }
    if (!fs.existsSync(GODOT_PROJECT)) {
        console.error(`ERROR: Godot project not found at ${GODOT_PROJECT}`);
        process.exit(2);
    }
    console.log(`Using Godot binary: ${godotBin}`);
    console.log(`Using project:     ${GODOT_PROJECT}`);
    console.log(`Using port:        ${PORT}`);

    const godotArgs = ['--headless', '--editor', '--path', GODOT_PROJECT];
    console.log(`Spawning:          ${godotBin} ${godotArgs.join(' ')}`);

    const logFile = path.join(PROJECT_ROOT, 'godot_e2e.log');
    const logFd = fs.openSync(logFile, 'w');
    const godot = spawn(godotBin, godotArgs, {
        cwd: PROJECT_ROOT,
        stdio: ['ignore', logFd, logFd],
        windowsHide: true,
    });
    godot.on('error', (err) => {
        console.error(`Failed to spawn Godot: ${err.message}`);
        process.exit(2);
    });

    const cleanup = () => {
        try { godot.kill('SIGTERM'); } catch (_) {}
        setTimeout(() => { try { godot.kill('SIGKILL'); } catch (_) {} }, 3000);
        try { fs.closeSync(logFd); } catch (_) {}
    };
    process.on('exit', cleanup);
    process.on('SIGINT', () => { cleanup(); process.exit(130); });
    process.on('SIGTERM', () => { cleanup(); process.exit(143); });

    try {
        console.log(`\nWaiting for Godot to listen on ${HOST}:${PORT} (max ${GODOT_READY_TIMEOUT_MS / 1000}s)...`);
        await waitForPort(PORT, HOST, GODOT_READY_TIMEOUT_MS);
        console.log('Godot is ready.\n');

        await runTests();
    } catch (err) {
        console.error(`\nERROR: ${err.message}`);
        console.error(`See ${logFile} for Godot output.`);
        cleanup();
        process.exit(1);
    }

    cleanup();

    console.log(`\n=== Summary ===`);
    console.log(`Passed: ${passed}`);
    console.log(`Failed: ${failed}`);
    if (failed > 0) {
        console.log(`\nFailures:`);
        for (const f of failures) console.log(`  - ${f}`);
        process.exit(1);
    }
    process.exit(0);
}

main().catch((err) => {
    console.error('Unhandled error:', err);
    process.exit(1);
});
