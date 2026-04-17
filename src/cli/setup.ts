/**
 * Gear setup — Install shell hooks into ~/.bashrc or ~/.zshrc
 *
 * Wraps AI CLI tools (claude, codex, gemini, opencode, omc, omx)
 * with a precheck function that displays cached Gear update notifications.
 */

import { existsSync, readFileSync, writeFileSync, appendFileSync } from 'fs';
import {
  getShellRcFile,
  getShellName,
  getLocalVersion,
  ensureGearDir,
  ONBOARDING_SHOWN_FILE,
  STAR_PROMPTED_FILE,
  supportsShellHooks,
} from './utils.js';

const MARKER_START = '# >>> Gear shell hooks >>>';
const MARKER_END = '# <<< Gear shell hooks <<<';

/** The shell hook block that gets appended to the RC file. */
function generateHookBlock(): string {
  // Target commands (excluding omx — handled separately)
  const targetList = 'claude codex gemini opencode omc';

  const lines: string[] = [
    MARKER_START,
    '# Gear update notifications for AI CLI tools',
    '# Installed by: Gear setup | Remove with: Gear uninstall',
    '',
    '__Gear_precheck() {',
    '  local notify="$HOME/.Gear/notify"',
    '  local star="$HOME/.Gear/star-prompted"',
    '  # If notification exists or star not yet prompted → interactive prompt',
    '  if [ -f "$notify" ] || [ ! -f "$star" ]; then',
    '    command -v Gear &>/dev/null && Gear notify',
    '  fi',
    '  # Background refresh for next time',
    '  local ts="$HOME/.Gear/last-check"',
    '  if [ -f "$ts" ]; then',
    '    local age=$(( $(date +%s) - $(cat "$ts") ))',
    '    [ "$age" -lt 86400 ] && return',
    '  fi',
    '  command -v Gear &>/dev/null && Gear check --bg &>/dev/null & disown &>/dev/null',
    '}',

    '',
    '# Wrap AI CLI tools: precheck \u2192 original command',
    `for __Gear_cmd in ${targetList}; do`,
    '  if command -v "$__Gear_cmd" &>/dev/null && ! declare -f "$__Gear_cmd" &>/dev/null; then',
    '    eval "${__Gear_cmd}() { __Gear_precheck; command ${__Gear_cmd} \\"\\$@\\"; }"',
    '  fi',
    'done',
    '',
    '# omx: preserve existing function (e.g. --no-alt-screen wrapper)',
    'if declare -f omx &>/dev/null; then',
    '  eval "__Gear_orig_omx() $(declare -f omx | sed \'1d\')"',
    '  omx() { __Gear_precheck; __Gear_orig_omx "$@"; }',
    'elif command -v omx &>/dev/null; then',
    '  omx() { __Gear_precheck; command omx "$@"; }',
    'fi',
    '',
    'unset __Gear_cmd',
    MARKER_END,
  ];

  return lines.join('\n');
}

export async function setupShellHooks(args: string[] = []): Promise<void> {
  const silent = args.includes('--silent');
  if (!supportsShellHooks()) {
    if (!silent) {
      console.log('ℹ️  Gear shell hooks are only installed for bash/zsh on Unix-like systems.');
      console.log('   Skipping shell hook setup on this platform/shell.');
    }
    return;
  }

  const rcFile = getShellRcFile();
  const shellName = getShellName();

  const log = silent ? (..._args: any[]) => {} : console.log.bind(console);

  // Check if RC file exists
  if (!existsSync(rcFile)) {
    log(`⚠️  ${rcFile} not found. Creating it.`);
    writeFileSync(rcFile, '');
  }

  const content = readFileSync(rcFile, 'utf-8');

  // Check if already installed
  if (content.includes(MARKER_START)) {
    const cleaned = removeHookBlock(content);
    const hookBlock = generateHookBlock();
    writeFileSync(rcFile, cleaned + '\n' + hookBlock + '\n');
    log(`🔄 Gear shell hooks updated in ${rcFile}`);
  } else {
    const hookBlock = generateHookBlock();
    appendFileSync(rcFile, '\n' + hookBlock + '\n');
    log(`✅ Gear shell hooks installed in ${rcFile}`);
  }

  log(`   Reload with: source ${rcFile}`);
  log('');

  // Show onboarding (once)
  ensureGearDir();
  if (!existsSync(ONBOARDING_SHOWN_FILE)) {
    printOnboarding(log);
    writeFileSync(ONBOARDING_SHOWN_FILE, new Date().toISOString());
  }

  // Suggest star (once)
  if (!existsSync(STAR_PROMPTED_FILE)) {
    log('⭐ If Gear helps your Godot workflow, please star us!');
    log('   Run: Gear star');
    log('');
  }
}

function removeHookBlock(content: string): string {
  const regex = new RegExp(
    `\\n?${escapeRegex(MARKER_START)}[\\s\\S]*?${escapeRegex(MARKER_END)}\\n?`,
    'g'
  );
  return content.replace(regex, '');
}

function escapeRegex(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function printOnboarding(log: (...args: any[]) => void = console.log): void {
  const version = getLocalVersion();
  log('╔══════════════════════════════════════════════════════╗');
  log(`║  🎮 Gear v${version} — AI-Powered Godot Development`
    + ' '.repeat(Math.max(0, 39 - version.length)) + '║');
  log('║                                                      ║');
  log('║  110+ tools for Godot Engine via MCP                 ║');
  log('║                                                      ║');
  log('║  📖 Docs:   https://github.com/wvfp/Gear-godot-mcp   ║');
  log('║  ⭐ Star:   Gear star                              ║');
  log('║  🔄 Update: npm update -g Gear                     ║');
  log('╚══════════════════════════════════════════════════════╝');
  log('');
}

export { MARKER_START, MARKER_END };
