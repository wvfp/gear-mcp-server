import { existsSync } from 'fs';
import { join, normalize } from 'path';
import { exec } from 'child_process';
import { promisify } from 'util';
import { homedir } from 'os';

const execAsync = promisify(exec);

interface DiagnosticResult {
  category: string;
  status: 'pass' | 'warn' | 'fail';
  message: string;
  details?: string[];
  suggestion?: string;
}

export async function runDiagnostics(): Promise<void> {
  console.log('Running Gear diagnostics...\n');

  const results: DiagnosticResult[] = [];

  results.push(...await checkNodeVersion());
  results.push(...await checkGodotInstallation());
  results.push(...await checkNetworkConnectivity());
  results.push(...await checkPortAvailability());
  results.push(...await checkEnvironmentVariables());

  let passCount = 0;
  let warnCount = 0;
  let failCount = 0;

  for (const result of results) {
    switch (result.status) {
      case 'pass': passCount++; break;
      case 'warn': warnCount++; break;
      case 'fail': failCount++; break;
    }
  }

  console.log('─'.repeat(60));
  console.log('\nSummary:');
  console.log(`  ✅ Pass:  ${passCount}`);
  console.log(`  ⚠️  Warn:  ${warnCount}`);
  console.log(`  ❌ Fail:  ${failCount}`);
  console.log('');

  if (failCount > 0) {
    console.log('Please fix the failing items before using Gear.\n');
    process.exit(1);
  } else if (warnCount > 0) {
    console.log('Some warnings were found. Gear should work but may have limited functionality.\n');
  } else {
    console.log('All checks passed! Gear is ready to use.\n');
  }
}

async function checkNodeVersion(): Promise<DiagnosticResult[]> {
  const results: DiagnosticResult[] = [];
  const version = process.version;
  const major = parseInt(version.slice(1).split('.')[0], 10);

  if (major >= 18) {
    results.push({
      category: 'Node.js',
      status: 'pass',
      message: `Node.js ${version} is installed`,
      details: [`Required: >=18, Found: ${version}`],
    });
  } else {
    results.push({
      category: 'Node.js',
      status: 'fail',
      message: `Node.js version is too old`,
      details: [`Required: >=18, Found: ${version}`],
      suggestion: 'Update Node.js to version 18 or higher',
    });
  }

  return results;
}

async function checkGodotInstallation(): Promise<DiagnosticResult[]> {
  const results: DiagnosticResult[] = [];

  const godotPath = process.env.GODOT_PATH;
  if (godotPath) {
    if (existsSync(godotPath)) {
      try {
        const { stdout } = await execAsync(`"${godotPath}" --version`);
        results.push({
          category: 'Godot',
          status: 'pass',
          message: `Godot found at ${godotPath}`,
          details: [stdout.trim()],
        });
      } catch {
        results.push({
          category: 'Godot',
          status: 'fail',
          message: `GODOT_PATH is set but executable is invalid`,
          details: [`Path: ${godotPath}`],
          suggestion: 'Set a valid GODOT_PATH or remove the environment variable',
        });
      }
    } else {
      results.push({
        category: 'Godot',
        status: 'fail',
        message: `GODOT_PATH is set but file does not exist`,
        details: [`Path: ${godotPath}`],
        suggestion: 'Set a valid GODOT_PATH or remove the environment variable',
      });
    }
    return results;
  }

  const platform = process.platform;
  const possiblePaths: string[] = ['godot'];

  if (platform === 'darwin') {
    possiblePaths.push(
      '/Applications/Godot.app/Contents/MacOS/Godot',
      '/Applications/Godot_4.app/Contents/MacOS/Godot',
    );
  } else if (platform === 'win32') {
    possiblePaths.push(
      'C:\\Program Files\\Godot\\Godot.exe',
      'C:\\Program Files (x86)\\Godot\\Godot.exe',
    );
  } else if (platform === 'linux') {
    possiblePaths.push('/usr/bin/godot', '/usr/local/bin/godot');
  }

  let found = false;
  for (const path of possiblePaths) {
    try {
      const { stdout } = await execAsync(path === 'godot' ? 'godot --version' : `"${path}" --version`);
      results.push({
        category: 'Godot',
        status: 'pass',
        message: `Godot found: ${path}`,
        details: [stdout.trim()],
      });
      found = true;
      break;
    } catch {
      // Continue checking
    }
  }

  if (!found) {
    results.push({
      category: 'Godot',
      status: 'warn',
      message: 'Godot not found in common locations',
      details: [`Searched: ${possiblePaths.join(', ')}`],
      suggestion: 'Set GODOT_PATH environment variable or install Godot',
    });
  }

  return results;
}

async function checkNetworkConnectivity(): Promise<DiagnosticResult[]> {
  const results: DiagnosticResult[] = [];

  try {
    const { execSync } = await import('child_process');
    execSync('curl -s --connect-timeout 5 https://godotengine.org -o /dev/null', { stdio: 'ignore' });
    results.push({
      category: 'Network',
      status: 'pass',
      message: 'Network connectivity is working',
    });
  } catch {
    results.push({
      category: 'Network',
      status: 'warn',
      message: 'Cannot reach godotengine.org',
      suggestion: 'Check your internet connection if asset downloads are needed',
    });
  }

  return results;
}

async function checkPortAvailability(): Promise<DiagnosticResult[]> {
  const results: DiagnosticResult[] = [];
  const defaultPort = parseInt(process.env.GODOT_BRIDGE_PORT || process.env.MCP_BRIDGE_PORT || '6505', 10);

  try {
    const { execSync } = await import('child_process');
    if (process.platform === 'win32') {
      execSync(`netstat -ano | findstr :${defaultPort}`, { stdio: 'ignore' });
    } else {
      execSync(`lsof -i :${defaultPort} || true`, { stdio: 'ignore' });
    }
    results.push({
      category: 'Port',
      status: 'warn',
      message: `Port ${defaultPort} may be in use`,
      suggestion: `If Gear fails to start, another process may be using port ${defaultPort}`,
    });
  } catch {
    results.push({
      category: 'Port',
      status: 'pass',
      message: `Port ${defaultPort} appears to be available`,
    });
  }

  return results;
}

async function checkEnvironmentVariables(): Promise<DiagnosticResult[]> {
  const results: DiagnosticResult[] = [];
  const relevantVars = ['GODOT_PATH', 'Gear_TOOL_PROFILE', 'GODOT_BRIDGE_PORT', 'DEBUG', 'LOG_MODE'];

  const setVars: string[] = [];
  const missingVars: string[] = [];

  for (const v of relevantVars) {
    if (process.env[v]) {
      setVars.push(`${v}=${process.env[v]}`);
    } else {
      missingVars.push(v);
    }
  }

  if (setVars.length > 0) {
    results.push({
      category: 'Environment',
      status: 'pass',
      message: 'Environment variables:',
      details: setVars,
    });
  }

  if (missingVars.length > 0 && missingVars.includes('GODOT_PATH')) {
    results.push({
      category: 'Environment',
      status: 'warn',
      message: 'GODOT_PATH not set',
      suggestion: 'Godot will be auto-detected, or set GODOT_PATH for reliability',
    });
  }

  return results;
}
