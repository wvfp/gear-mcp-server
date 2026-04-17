import { spawn, ChildProcess } from 'child_process';
import { existsSync } from 'fs';
import { join, normalize } from 'path';
import { promisify } from 'util';
import { exec } from 'child_process';
import type { GodotProcess } from '../server-types.js';
import { GODOT_DEBUG_MODE_DEFAULT } from '../server-version.js';

const execAsync = promisify(exec);

export class LifecycleManager {
  private activeProcess: GodotProcess | null = null;
  private godotPath: string | null = null;
  private validatedPaths: Map<string, boolean> = new Map();
  private godotDebugMode: boolean = GODOT_DEBUG_MODE_DEFAULT;
  private operationsScriptPath: string;
  private lastProjectPath: string | null = null;

  constructor(operationsScriptPath: string) {
    this.operationsScriptPath = operationsScriptPath;
  }

  setGodotPath(path: string | null): void {
    this.godotPath = path;
  }

  getGodotPath(): string | null {
    return this.godotPath;
  }

  setDebugMode(mode: boolean): void {
    this.godotDebugMode = mode;
  }

  setLastProjectPath(path: string | null): void {
    this.lastProjectPath = path;
  }

  getLastProjectPath(): string | null {
    return this.lastProjectPath;
  }

  getActiveProcess(): GodotProcess | null {
    return this.activeProcess;
  }

  setActiveProcess(process: GodotProcess | null): void {
    this.activeProcess = process;
  }

  async validateGodotPath(path: string): Promise<boolean> {
    if (this.validatedPaths.has(path)) {
      return this.validatedPaths.get(path)!;
    }

    try {
      if (path !== 'godot' && !existsSync(path)) {
        this.validatedPaths.set(path, false);
        return false;
      }

      const command = path === 'godot' ? 'godot --version' : `"${path}" --version`;
      await execAsync(command);
      this.validatedPaths.set(path, true);
      return true;
    } catch {
      this.validatedPaths.set(path, false);
      return false;
    }
  }

  async detectGodotPath(): Promise<string | null> {
    if (this.godotPath && await this.validateGodotPath(this.godotPath)) {
      return this.godotPath;
    }

    const envPath = process.env.GODOT_PATH;
    if (envPath) {
      const normalized = normalize(envPath);
      if (await this.validateGodotPath(normalized)) {
        this.godotPath = normalized;
        return this.godotPath;
      }
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

    for (const path of possiblePaths) {
      const normalized = normalize(path);
      if (await this.validateGodotPath(normalized)) {
        this.godotPath = normalized;
        return this.godotPath;
      }
    }

    return null;
  }

  spawnGodotProcess(args: string[], onError?: (err: Error) => void): ChildProcess | null {
    if (!this.godotPath) {
      return null;
    }

    const process = spawn(this.godotPath, args, {
      stdio: 'pipe',
    });

    process.on('error', (err: Error) => {
      if (onError) {
        onError(err);
      }
    });

    return process;
  }

  async cleanup(): Promise<void> {
    if (this.activeProcess) {
      try {
        this.activeProcess.process.kill();
      } catch {}
      this.activeProcess = null;
    }
  }

  getOperationsScriptPath(): string {
    return this.operationsScriptPath;
  }
}
