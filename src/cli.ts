#!/usr/bin/env node
/**
 * Gear CLI Entrypoint
 *
 * Routes subcommands or falls through to the MCP server.
 *
 *   Gear              → Start MCP server (default, backward-compatible)
 *   Gear setup        → Install shell hooks
 *   Gear check        → Check for updates
 *   Gear doctor       → Run environment diagnostics
 *   Gear profile      → Performance profiling utilities
 *   Gear star         → Star on GitHub
 *   Gear uninstall    → Remove shell hooks
 *   Gear version      → Print version
 *   Gear help         → Show help
 */

import { getLocalVersion } from './cli/utils.js';

const args = process.argv.slice(2);
const command = args[0];

const CLI_COMMANDS = ['setup', 'check', 'star', 'notify', 'uninstall', 'version', 'help', 'doctor', 'profile', '--version', '-v', '--help', '-h'];

async function main(): Promise<void> {
  // If no args or not a CLI command → start MCP server (original behavior)
  if (!command || !CLI_COMMANDS.includes(command)) {
    // Dynamic import to avoid loading MCP SDK for CLI-only commands
    await import('./index.js');
    return;
  }

  switch (command) {
    case 'setup': {
      const { setupShellHooks } = await import('./cli/setup.js');
      await setupShellHooks(args.slice(1));
      break;
    }
    case 'check': {
      const { checkForUpdates } = await import('./cli/check.js');
      await checkForUpdates(args.slice(1));
      break;
    }
    case 'star': {
      const { starGear } = await import('./cli/star.js');
      await starGear();
      break;
    }
    case 'notify': {
      const { showNotification } = await import('./cli/notify.js');
      await showNotification();
      break;
    }
    case 'uninstall': {
      const { uninstallHooks } = await import('./cli/uninstall.js');
      await uninstallHooks();
      break;
    }
    case 'doctor': {
      const { runDiagnostics } = await import('./cli/doctor.js');
      await runDiagnostics();
      break;
    }
    case 'profile': {
      const { runProfileCommand } = await import('./cli/profile.js');
      await runProfileCommand(args.slice(1));
      break;
    }
    case 'version':
    case '--version':
    case '-v': {
      console.log(`Gear v${getLocalVersion()}`);
      break;
    }
    case 'help':
    case '--help':
    case '-h': {
      printHelp();
      break;
    }
  }
}

function printHelp(): void {
  const version = getLocalVersion();
  console.log(`
Gear v${version} — AI-Powered Godot Development via MCP

Usage:
  Gear                Start MCP server (default)
  Gear setup          Install shell hooks for update notifications
  Gear check          Check for Gear updates
  Gear check --bg     Background check (used by shell hooks)
  Gear check --quiet  Print only if update available
  Gear doctor         Run environment diagnostics
  Gear profile        Performance profiling utilities
  Gear star           Star Gear on GitHub
  Gear uninstall      Remove shell hooks
  Gear version        Show current version
  Gear help           Show this help

Shell hooks wrap these commands with update notifications:
  claude, codex, gemini, opencode, omc, omx

More info: https://github.com/HaD0Yun/Gear-godot-mcp
`.trim());
}

main().catch((err) => {
  console.error('Gear:', err.message ?? err);
  process.exit(1);
});
