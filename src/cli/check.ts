/**
 * Gear check — Update check logic
 *
 * Modes:
 *   Gear check         Interactive: show update status
 *   Gear check --bg    Background: refresh cache silently, exit
 *   Gear check --quiet Print one-liner only if update available
 */

import {
  getLocalVersion,
  fetchLatestVersion,
  compareSemver,
  isCacheFresh,
  updateCacheTimestamp,
  writeNotifyFile,
  clearNotifyFile,
  ensureGearDir,
} from './utils.js';

export async function checkForUpdates(args: string[]): Promise<void> {
  const isBg = args.includes('--bg');
  const isQuiet = args.includes('--quiet');

  ensureGearDir();

  // Background mode: refresh cache and exit silently
  if (isBg) {
    await backgroundCheck();
    return;
  }

  // Interactive or quiet mode: always fetch fresh
  const currentVersion = getLocalVersion();
  const latestVersion = await fetchLatestVersion();

  if (!latestVersion) {
    if (!isQuiet) {
      console.log('⚠️  Could not reach npm registry. Check your network.');
    }
    return;
  }

  if (compareSemver(latestVersion, currentVersion) > 0) {
    if (isQuiet) {
      console.log(`🚀 Gear v${latestVersion} available! Run: npm update -g Gear`);
    } else {
      printUpdateBox(currentVersion, latestVersion);
    }
  } else {
    if (!isQuiet) {
      console.log(`✅ Gear v${currentVersion} is up to date.`);
    }
  }
}

/** Background check: called by shell hook (once per day). */
async function backgroundCheck(): Promise<void> {
  // Skip if cache is fresh (< 24h)
  if (isCacheFresh()) {
    return;
  }

  const currentVersion = getLocalVersion();
  const latestVersion = await fetchLatestVersion();

  // Update timestamp regardless of result (avoid hammering on failure)
  updateCacheTimestamp();

  if (!latestVersion) return;

  if (compareSemver(latestVersion, currentVersion) > 0) {
    const msg = `🚀 Gear v${latestVersion} available! (current: v${currentVersion})\n   Run: npm update -g Gear`;
    writeNotifyFile(msg);
  } else {
    // No update — clear stale notification if any
    clearNotifyFile();
  }
}

function printUpdateBox(current: string, latest: string): void {
  const line1 = `  🚀 Gear v${latest} available! (current: v${current})`;
  const line2 = `  npm update -g Gear`;
  const line3 = `  https://github.com/HaD0Yun/Gear-godot-mcp/releases`;
  const maxLen = Math.max(line1.length, line2.length, line3.length) + 2;
  const pad = (s: string) => s + ' '.repeat(Math.max(0, maxLen - s.length));

  console.log('');
  console.log('╔' + '═'.repeat(maxLen) + '╗');
  console.log('║' + pad(line1) + '║');
  console.log('║' + ' '.repeat(maxLen) + '║');
  console.log('║' + pad(line2) + '║');
  console.log('║' + pad(line3) + '║');
  console.log('╚' + '═'.repeat(maxLen) + '╝');
  console.log('');
}
