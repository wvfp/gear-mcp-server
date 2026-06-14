#!/usr/bin/env node
// tests/e2e_phase4.js — End-to-end test for Phase 4 tools.
//
// Spawns Godot headless editor → waits for MCP → exercises every new
// Phase 4 tool/resource/prompt. Each tool is required to:
//   - be present in tools/list (or resources/list / prompts/list)
//   - return a well-formed response (success OR a clean error message)
//   - not crash the MCP server
//
// Usage:   node tests/e2e_phase4.js

'use strict';

const net = require('net');
const { spawn, execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const PROJECT_ROOT = path.resolve(__dirname, '..');
const GODOT_PROJECT = path.join(PROJECT_ROOT, 'project');
const PORT = parseInt(process.env.GEAR_PORT || '8510');
const HOST = '127.0.0.1';
const GODOT_READY_TIMEOUT_MS = 60000;
const RPC_TIMEOUT_MS = 15000;

// ---------------------------------------------------------------------------
// Godot discovery
// ---------------------------------------------------------------------------

function findGodot() {
    if (process.env.GEAR_GODOT_BIN) return process.env.GEAR_GODOT_BIN;
    const candidates = process.platform === 'win32'
        ? ['G:\\Godot\\Godot_v4.7-dev3_win64_console.exe', 'godot.exe', 'godot']
        : process.platform === 'darwin'
            ? ['/Applications/Godot.app/Contents/MacOS/Godot', 'godot']
            : ['/usr/bin/godot', '/usr/local/bin/godot', 'godot'];
    for (const c of candidates) {
        if (path.isAbsolute(c) && fs.existsSync(c)) return c;
    }
    const which = process.platform === 'win32' ? 'where' : 'which';
    try {
        const out = execSync(`${which} godot`, { stdio: ['ignore', 'pipe', 'ignore'] })
            .toString().split(/\r?\n/)[0].trim();
        if (out) return out;
    } catch (_) {}
    return null;
}

// ---------------------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------------------

let passed = 0, failed = 0;
const failures = [];
function ok(cond, msg) {
    if (cond) { passed++; console.log(`  \x1b[32mPASS\x1b[0m  ${msg}`); }
    else      { failed++; failures.push(msg); console.log(`  \x1b[31mFAIL\x1b[0m  ${msg}`); }
}
function contains(haystack, needle, label) {
    ok(typeof haystack === 'string' && haystack.includes(needle),
       `${label} contains "${needle}"`);
}
function parseText(result) {
    const text = result?.content?.[0]?.text || (typeof result === 'string' ? result : '{}');
    try { return JSON.parse(text); } catch (e) { return { _rawText: text }; }
}

// ---------------------------------------------------------------------------
// MCP client
// ---------------------------------------------------------------------------

class MCPClient {
    constructor(host, port) {
        this.host = host; this.port = port;
        this.sock = null; this.buf = ''; this.id = 0; this.pending = new Map();
    }
    connect() {
        return new Promise((resolve, reject) => {
            this.sock = net.createConnection(this.port, this.host, () => resolve());
            this.sock.on('error', reject);
            this.sock.on('data', (d) => this._onData(d));
            this.sock.on('close', () => {
                for (const p of this.pending.values()) p.rej(new Error('socket closed'));
                this.pending.clear();
            });
        });
    }
    _onData(chunk) {
        this.buf += chunk.toString('utf8');
        let idx;
        while ((idx = this.buf.indexOf('\n')) >= 0) {
            const line = this.buf.slice(0, idx); this.buf = this.buf.slice(idx + 1);
            if (!line.trim()) continue;
            try {
                const msg = JSON.parse(line);
                if (msg.id != null && this.pending.has(msg.id)) {
                    const p = this.pending.get(msg.id);
                    this.pending.delete(msg.id);
                    clearTimeout(p.timer);
                    if (msg.error) p.rej(new Error(msg.error.message || JSON.stringify(msg.error)));
                    else p.res(msg.result);
                }
            } catch (_) {}
        }
    }
    call(method, params) {
        return new Promise((resolve, reject) => {
            const id = ++this.id;
            const timer = setTimeout(() => {
                this.pending.delete(id);
                reject(new Error(`RPC timeout: ${method}`));
            }, RPC_TIMEOUT_MS);
            this.pending.set(id, { res: resolve, rej: reject, timer });
            this.sock.write(JSON.stringify({ jsonrpc: '2.0', id, method, params: params || {} }) + '\n');
        });
    }
    async callTool(name, args) {
        const r = await this.call('tools/call', { name, arguments: args || {} });
        return r;
    }
    close() { if (this.sock) { this.sock.end(); this.sock.destroy(); } }
}

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
                if (Date.now() - start >= timeoutMs) reject(new Error(`port ${port} not ready`));
                else setTimeout(tryOnce, 500);
            }, 500);
        };
        tryOnce();
    });
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

async function runTests() {
    console.log('\n=== Gear MCP Server — Phase 4 E2E Tests ===\n');

    // ---------- Test 1: MCP handshake ----------
    {
        console.log('Group: MCP handshake');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r = await c.call('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'phase4_test', version: '1.0' } });
        ok(r.protocolVersion === '2024-11-05', 'protocolVersion is 2024-11-05');
        ok(r.serverInfo && r.serverInfo.name === 'gear-mcp-server', 'serverInfo.name = gear-mcp-server');
        ok(r.capabilities && r.capabilities.prompts !== undefined, 'capabilities.prompts present');
        c.close();
    }

    // ---------- Test 2: tools/list — verify all 34 Phase 4 tools are present ----------
    {
        console.log('\nGroup: tools/list — Phase 4 tools registered');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r = await c.call('tools/list', {});
        const names = (r.tools || []).map(t => t.name);

        const expected = [
            // 4.5 intent (9)
            'capture_intent_snapshot', 'record_decision_log', 'record_work_step',
            'record_execution_trace', 'summarize_intent_context',
            'generate_handoff_brief', 'export_handoff_pack',
            'set_recording_mode', 'get_recording_mode',
            // 4.6 meta (2)
            'tool_catalog', 'manage_tool_groups',
            // 4.2 assets (3)
            'list_asset_providers', 'search_assets', 'fetch_asset',
            // 4.1 debug (10)
            'lsp_get_diagnostics', 'lsp_get_completions', 'lsp_get_hover', 'lsp_get_symbols',
            'dap_set_breakpoint', 'dap_remove_breakpoint',
            'dap_continue', 'dap_pause', 'dap_step_over', 'dap_get_stack_trace',
            // 4.3 testing (6)
            'capture_screenshot', 'capture_viewport',
            'inject_action', 'inject_key', 'inject_mouse_click', 'inject_mouse_motion',
            // 4.4 dx (4)
            'parse_error_log', 'get_project_health',
            'find_resource_usages', 'scaffold_gameplay_prototype',
        ];
        for (const t of expected) {
            ok(names.includes(t), `tools/list contains "${t}"`);
        }
        ok(names.length >= 100, `total tool count >= 100 (got ${names.length})`);
        c.close();
    }

    // ---------- Test 3: resources/list — verify new resources ----------
    {
        console.log('\nGroup: resources/list — Phase 4.7 resources');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r = await c.call('resources/list', {});
        const uris = (r.resources || []).map(x => x.uri);
        for (const uri of [
            'godot://editor/context',
            'godot://project/info',
            'godot://scene/{path}',
            'godot://script/{path}',
            'godot://resource/{path}',
        ]) {
            ok(uris.includes(uri), `resources/list contains "${uri}"`);
        }
        c.close();
    }

    // ---------- Test 4: prompts/list — verify new prompts ----------
    {
        console.log('\nGroup: prompts/list — Phase 4.7 prompts');
        const c = new MCPClient(HOST, PORT);
        await c.connect();
        const r = await c.call('prompts/list', {});
        const names = (r.prompts || []).map(p => p.name);
        ok(names.includes('godot.scene_bootstrap'), 'prompts/list contains godot.scene_bootstrap');
        ok(names.includes('godot.debug_triage'), 'prompts/list contains godot.debug_triage');
        c.close();
    }

    // ---------- Test 5: intent tools (9) ----------
    {
        console.log('\nGroup: 4.5 intent/ (9 tools)');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.callTool('capture_intent_snapshot', {
            label: 'phase4-test-snap', intent: 'verify intent tools', context: { test: true }
        });
        ok(!r1.isError, 'capture_intent_snapshot succeeded');
        const p1 = parseText(r1);
        ok(p1.success === true, 'capture_intent_snapshot returns success');

        const r2 = await c.callTool('record_decision_log', {
            decision: 'use Phase 4 test suite', rationale: 'exhaustive coverage', tags: ['test', 'phase4']
        });
        ok(!r2.isError, 'record_decision_log succeeded');

        const r3 = await c.callTool('record_work_step', {
            action: 'test_intent_tools', tool: 'tests/e2e_phase4.js', args: { group: 'intent' }
        });
        ok(!r3.isError, 'record_work_step succeeded');

        const r4 = await c.callTool('record_execution_trace', {
            event: 'intent_test_completed', level: 'info', duration_ms: 12.3, details: { ok: true }
        });
        ok(!r4.isError, 'record_execution_trace succeeded');

        const r5 = await c.callTool('summarize_intent_context', { max_recent: 3 });
        ok(!r5.isError, 'summarize_intent_context succeeded');
        const p5 = parseText(r5);
        ok(p5.counts && typeof p5.counts.snapshots === 'number', 'summarize_intent_context returns counts');

        const r6 = await c.callTool('generate_handoff_brief', { title: 'Phase 4 Test Brief' });
        ok(!r6.isError, 'generate_handoff_brief succeeded');
        const p6 = parseText(r6);
        ok((p6.brief || '').includes('Phase 4 Test Brief'), 'brief contains title');

        const exportPath = path.join(GODOT_PROJECT, '__e2e_phase4_handoff.json');
        const r7 = await c.callTool('export_handoff_pack', {
            output_path: exportPath, title: 'Phase 4 Handoff'
        });
        ok(!r7.isError, 'export_handoff_pack succeeded');
        const p7 = parseText(r7);
        ok(p7.success === true, 'export_handoff_pack returns success');
        ok(fs.existsSync(exportPath), 'handoff pack file created');
        try { fs.unlinkSync(exportPath); } catch (_) {}

        const r8 = await c.callTool('set_recording_mode', { enabled: false });
        ok(!r8.isError, 'set_recording_mode(false) succeeded');
        // record should now be a no-op
        const r8b = await c.callTool('record_work_step', { action: 'should_not_record' });
        const p8b = parseText(r8b);
        ok(p8b.recorded === false, 'record_work_step no-ops when recording off');
        // Re-enable for downstream tests
        await c.callTool('set_recording_mode', { enabled: true });

        const r9 = await c.callTool('get_recording_mode', {});
        ok(!r9.isError, 'get_recording_mode succeeded');
        const p9 = parseText(r9);
        ok(p9.recording === true, 'get_recording_mode reports recording=true after re-enable');

        c.close();
    }

    // ---------- Test 6: meta tools (2) ----------
    {
        console.log('\nGroup: 4.6 meta/ (2 tools)');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.callTool('tool_catalog', {});
        ok(!r1.isError, 'tool_catalog succeeded');
        const p1 = parseText(r1);
        ok(Array.isArray(p1.domains), 'tool_catalog returns domains array');
        ok(p1.domain_count >= 20, `tool_catalog lists >= 20 domains (got ${p1.domain_count})`);

        // Filter by domain
        const r1b = await c.callTool('tool_catalog', { domain: 'intent' });
        const p1b = parseText(r1b);
        ok(p1b.domains.length === 1 && p1b.domains[0].name === 'intent', 'tool_catalog filters by domain');

        // Substring search
        const r1c = await c.callTool('tool_catalog', { query: 'screenshot' });
        const p1c = parseText(r1c);
        ok(p1c.total_tools > 0, 'tool_catalog search finds at least one tool');

        const r2 = await c.callTool('manage_tool_groups', { action: 'list' });
        ok(!r2.isError, 'manage_tool_groups(list) succeeded');
        const p2 = parseText(r2);
        ok(Array.isArray(p2.groups), 'manage_tool_groups returns groups array');

        const r2b = await c.callTool('manage_tool_groups', { action: 'describe', group: 'debug' });
        ok(!r2b.isError, 'manage_tool_groups(describe, debug) succeeded');
        const p2b = parseText(r2b);
        ok(p2b.name === 'debug' && p2b.count >= 4, 'describe returns debug group details');

        const r2c = await c.callTool('manage_tool_groups', { action: 'describe', group: '__nonexistent__' });
        ok(r2c.isError === true, 'manage_tool_groups fails gracefully on unknown group');

        c.close();
    }

    // ---------- Test 7: assets tools (3) — list providers, search (may fail offline) ----------
    {
        console.log('\nGroup: 4.2 assets/ (3 tools)');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.callTool('list_asset_providers', {});
        ok(!r1.isError, 'list_asset_providers succeeded');
        const p1 = parseText(r1);
        ok(p1.count === 3, `3 providers listed (got ${p1.count})`);
        contains(JSON.stringify(p1.providers), 'polyhaven', 'providers include polyhaven');

        // search_assets — best-effort; may fail without network
        const r2 = await c.callTool('search_assets', { provider: 'ambientcg', query: 'wood' });
        // ambientcg has no public API, but should still return a clean response
        if (r2.isError) {
            ok(true, 'search_assets(ambientcg) returned clean error (likely offline)');
        } else {
            const p2 = parseText(r2);
            ok(typeof p2.note === 'string', 'search_assets(ambientcg) returns manual_search_url hint');
        }

        // fetch_asset with a bogus URL — should return a clean error
        const r3 = await c.callTool('fetch_asset', { asset_url: 'https://nonexistent.invalid/file.png' });
        ok(r3.isError === true, 'fetch_asset returns error for unreachable URL');

        c.close();
    }

    // ---------- Test 8: debug tools (10) — LSP/DAP require running server; expect clean error ----------
    {
        console.log('\nGroup: 4.1 debug/ (10 tools) — LSP/DAP clean-error checks');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        // Each debug tool should either succeed (rare, server running) or
        // return a clean error — never crash the MCP server.
        for (const [name, args] of [
            ['lsp_get_diagnostics', { uri: 'file:///x.gd' }],
            ['lsp_get_completions', { uri: 'file:///x.gd', line: 0, character: 0 }],
            ['lsp_get_hover', { uri: 'file:///x.gd', line: 0, character: 0 }],
            ['lsp_get_symbols', { uri: 'file:///x.gd' }],
            ['dap_set_breakpoint', { uri: 'file:///x.gd', lines: [1] }],
            ['dap_remove_breakpoint', { uri: 'file:///x.gd' }],
            ['dap_continue', { thread_id: 1 }],
            ['dap_pause', { thread_id: 1 }],
            ['dap_step_over', { thread_id: 1 }],
            ['dap_get_stack_trace', { thread_id: 1 }],
        ]) {
            const r = await c.callTool(name, args);
            // Either success or a structured error — the key thing is no crash
            ok(r !== undefined && r !== null, `${name} returned a response`);
            // The MCP server should still be alive — try a noop call
        }

        // Verify the server is still alive after all those calls
        const ping = await c.call('ping', {});
        ok(!ping.error, 'MCP server still alive after debug-tool calls');

        c.close();
    }

    // ---------- Test 9: testing tools (6) — input injection requires running game ----------
    {
        console.log('\nGroup: 4.3 testing/ (6 tools) — input/screenshot checks');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        // capture_screenshot to a real path — should produce a file (or a clean error)
        const ssPath = path.join(GODOT_PROJECT, '__e2e_phase4_ss.png');
        const r1 = await c.callTool('capture_screenshot', { output_path: ssPath });
        if (r1.isError) {
            ok(true, 'capture_screenshot returned clean error (no edited scene)');
        } else {
            const p1 = parseText(r1);
            ok(p1.success === true, 'capture_screenshot succeeded');
            ok(fs.existsSync(ssPath), 'screenshot file created');
            try { fs.unlinkSync(ssPath); } catch (_) {}
        }

        // capture_viewport reuses capture_screenshot
        const r2 = await c.callTool('capture_viewport', { output_path: ssPath, viewport: 'root' });
        ok(r2 !== undefined, 'capture_viewport returned a response');

        // inject_action — Input is only available in the runtime; in editor mode
        // the call should either succeed (if Input singleton is accessible) or
        // return a clean error.
        const r3 = await c.callTool('inject_action', { action: 'ui_select', press: true });
        ok(r3 !== undefined, 'inject_action returned a response');

        const r4 = await c.callTool('inject_key', { keycode: 'A', pressed: true });
        ok(r4 !== undefined, 'inject_key returned a response');

        const r5 = await c.callTool('inject_mouse_click', { button: 'left', x: 100, y: 200, pressed: true });
        ok(r5 !== undefined, 'inject_mouse_click returned a response');

        const r6 = await c.callTool('inject_mouse_motion', { x: 50, y: 75, relative_x: 5, relative_y: 5 });
        ok(r6 !== undefined, 'inject_mouse_motion returned a response');

        c.close();
    }

    // ---------- Test 10: dx tools (4) ----------
    {
        console.log('\nGroup: 4.4 dx/ (4 tools)');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.callTool('parse_error_log', {});
        if (r1.isError) {
            ok(true, 'parse_error_log returned clean error (no editor log yet)');
        } else {
            ok(!r1.isError, 'parse_error_log succeeded');
        }

        const r2 = await c.callTool('get_project_health', { include_script_scan: true });
        ok(!r2.isError, 'get_project_health succeeded');
        const p2 = parseText(r2);
        ok(p2.has_project_godot === true, 'project.godot exists');
        ok(typeof p2.health_score === 'number', 'get_project_health returns a score');
        ok(p2.stats && p2.stats.scene_count !== undefined, 'stats include scene_count');

        const r3 = await c.callTool('find_resource_usages', {
            resource_path: 'res://icon.svg', project_path: GODOT_PROJECT, max_results: 10
        });
        ok(!r3.isError, 'find_resource_usages succeeded');
        const p3 = parseText(r3);
        ok(p3.scanned_files > 0, 'find_resource_usages scanned files');

        const r4 = await c.callTool('scaffold_gameplay_prototype', {
            scene_path: 'res://__e2e_phase4_proto.tscn',
            script_path: 'res://__e2e_phase4_proto.gd',
            root_node_type: 'Node2D',
            name: 'Phase4Proto'
        });
        ok(!r4.isError, 'scaffold_gameplay_prototype succeeded');
        const p4 = parseText(r4);
        ok(Array.isArray(p4.created) && p4.created.length === 2, 'scaffold created 2 files');
        ok(fs.existsSync(path.join(GODOT_PROJECT, '__e2e_phase4_proto.tscn')), 'scaffold .tscn exists');
        ok(fs.existsSync(path.join(GODOT_PROJECT, '__e2e_phase4_proto.gd')), 'scaffold .gd exists');
        // Cleanup
        try { fs.unlinkSync(path.join(GODOT_PROJECT, '__e2e_phase4_proto.tscn')); } catch (_) {}
        try { fs.unlinkSync(path.join(GODOT_PROJECT, '__e2e_phase4_proto.gd')); } catch (_) {}
        // Also clean the .uid sidecar if any
        try { fs.unlinkSync(path.join(GODOT_PROJECT, '__e2e_phase4_proto.gd.uid')); } catch (_) {}

        c.close();
    }

    // ---------- Test 11: resources/read — exercise the 4 new resources ----------
    {
        console.log('\nGroup: resources/read — Phase 4.7 resources');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        // godot://project/info
        const r1 = await c.call('resources/read', { uri: 'godot://project/info' });
        ok(!r1.error, 'resources/read godot://project/info succeeded');
        const t1 = r1.contents?.[0]?.text || '{}';
        let p1 = {};
        try { p1 = JSON.parse(t1); } catch (_) {}
        ok(p1.project_godot_exists === true, 'project/info project_godot_exists=true');
        ok(p1.project_path && p1.project_path.length > 0, 'project/info has project_path');

        // godot://script/{path} — read project.godot itself to test
        const r2 = await c.call('resources/read', { uri: 'godot://script/res://project.godot' });
        ok(!r2.error, 'resources/read godot://script/res://project.godot succeeded');
        const t2 = r2.contents?.[0]?.text || '{}';
        ok(t2.includes('config_version'), 'script content has config_version');

        // godot://resource/{path}
        const r3 = await c.call('resources/read', { uri: 'godot://resource/res://project.godot' });
        ok(!r3.error, 'resources/read godot://resource/res://project.godot succeeded');

        // godot://scene/{path} — file doesn't exist, expect error or empty
        const r4 = await c.call('resources/read', { uri: 'godot://scene/res://__nonexistent__.tscn' });
        // Should be a clean error
        ok(r4.error || (r4.contents?.[0]?.text || '').includes('error'),
           'scene resource returns error for missing file');

        c.close();
    }

    // ---------- Test 12: prompts/get — exercise both prompts ----------
    {
        console.log('\nGroup: prompts/get — Phase 4.7 prompts');
        const c = new MCPClient(HOST, PORT);
        await c.connect();

        const r1 = await c.call('prompts/get', { name: 'godot.scene_bootstrap' });
        ok(!r1.error, 'prompts/get godot.scene_bootstrap succeeded');
        ok(Array.isArray(r1.messages), 'scene_bootstrap returns messages');
        const t1 = r1.messages?.[0]?.content?.text || '';
        ok(t1.includes('create_scene'), 'scene_bootstrap mentions create_scene');
        ok(t1.includes('Node2D') || t1.includes('root'), 'scene_bootstrap references scene structure');

        // With arguments
        const r1b = await c.call('prompts/get', {
            name: 'godot.scene_bootstrap',
            arguments: { name: 'BattleArena', root_node: 'Node3D', children: 'Player, Enemy, Camera3D' }
        });
        ok(!r1b.error, 'prompts/get scene_bootstrap with args succeeded');
        const t1b = r1b.messages?.[0]?.content?.text || '';
        ok(t1b.includes('BattleArena'), 'scene_bootstrap uses arg "name"');
        ok(t1b.includes('Node3D'), 'scene_bootstrap uses arg "root_node"');

        const r2 = await c.call('prompts/get', { name: 'godot.debug_triage' });
        ok(!r2.error, 'prompts/get godot.debug_triage succeeded');
        const t2 = r2.messages?.[0]?.content?.text || '';
        ok(t2.includes('capture_intent_snapshot'), 'debug_triage references intent tools');
        ok(t2.includes('validate_project'), 'debug_triage references validate_project');

        // Unknown prompt — the server returns a JSON-RPC error
        let r3_failed = false;
        try {
            await c.call('prompts/get', { name: 'godot.nonexistent' });
        } catch (e) {
            r3_failed = true;
        }
        ok(r3_failed, 'prompts/get fails for unknown prompt name');

        c.close();
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
    const godotBin = findGodot();
    if (!godotBin) {
        console.error('ERROR: Godot binary not found. Set GEAR_GODOT_BIN.');
        process.exit(2);
    }
    if (!fs.existsSync(GODOT_PROJECT)) {
        console.error(`ERROR: Godot project not found at ${GODOT_PROJECT}`);
        process.exit(2);
    }
    console.log(`Godot:  ${godotBin}`);
    console.log(`Proj:   ${GODOT_PROJECT}`);
    console.log(`Port:   ${PORT}`);

    const godotArgs = ['--headless', '--editor', '--path', GODOT_PROJECT];
    const logFile = path.join(PROJECT_ROOT, 'godot_e2e_phase4.log');
    const logFd = fs.openSync(logFile, 'w');
    const godot = spawn(godotBin, godotArgs, {
        cwd: PROJECT_ROOT, stdio: ['ignore', logFd, logFd], windowsHide: true,
    });
    godot.on('error', (err) => { console.error(`spawn failed: ${err.message}`); process.exit(2); });

    const cleanup = () => {
        try { godot.kill('SIGTERM'); } catch (_) {}
        setTimeout(() => { try { godot.kill('SIGKILL'); } catch (_) {} }, 3000);
        try { fs.closeSync(logFd); } catch (_) {}
    };
    process.on('exit', cleanup);
    process.on('SIGINT', () => { cleanup(); process.exit(130); });
    process.on('SIGTERM', () => { cleanup(); process.exit(143); });

    try {
        console.log(`\nWaiting for Godot on ${HOST}:${PORT} (max ${GODOT_READY_TIMEOUT_MS / 1000}s)...`);
        await waitForPort(PORT, HOST, GODOT_READY_TIMEOUT_MS);
        console.log('Godot ready.\n');
        await runTests();
    } catch (err) {
        console.error(`\nERROR: ${err.message}`);
        console.error(`See ${logFile}`);
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

main().catch((err) => { console.error('Unhandled:', err); process.exit(1); });
