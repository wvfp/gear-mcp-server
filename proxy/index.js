#!/usr/bin/env node
// gear-mcp-proxy/index.js — stdio-to-TCP proxy for Gear MCP Server

const net = require('net');

const PORT = parseInt(process.env.GEAR_PORT || '8510');
const HOST = process.env.GEAR_HOST || '127.0.0.1';

const socket = new net.Socket();

// TCP -> stdout (GDExtension responses back to AI client)
socket.on('data', (chunk) => {
    process.stdout.write(chunk);
});

socket.on('close', () => process.exit(0));

// stdin -> TCP (AI client requests to GDExtension)
process.stdin.on('data', (chunk) => {
    socket.write(chunk);
});

process.stdin.on('close', () => socket.end());

// Connect to GDExtension TCP server (with retry, wait for Godot editor startup)
let retries = 0;
const MAX_RETRIES = 60; // up to 60 seconds

function connect() {
    socket.connect(PORT, HOST);
}

socket.on('connect', () => {
    retries = 0;
});

// Unified error handler: retry on ECONNREFUSED, exit on other errors
socket.on('error', (err) => {
    if (err.code === 'ECONNREFUSED' && retries < MAX_RETRIES) {
        retries++;
        setTimeout(connect, 1000);
    } else if (err.code === 'ECONNREFUSED') {
        process.stderr.write('gear-mcp-proxy: Godot editor not running or MCP server not started\n');
        process.exit(1);
    } else {
        process.stderr.write(`gear-mcp-proxy: ${err.message}\n`);
        process.exit(1);
    }
});

connect();
