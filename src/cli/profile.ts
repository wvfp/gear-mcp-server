import { performance, PerformanceObserver } from 'perf_hooks';

interface ProfileEntry {
  name: string;
  startTime: number;
  endTime?: number;
  duration?: number;
  metadata?: Record<string, unknown>;
}

interface ProfileStats {
  totalTime: number;
  entries: ProfileEntry[];
  summary: Record<string, { count: number; totalDuration: number; avgDuration: number }>;
}

class Profiler {
  private entries: Map<string, ProfileEntry> = new Map();
  private enabled: boolean = false;

  enable(): void {
    this.enabled = true;
    console.log('[Profile] Profiler enabled');
  }

  disable(): void {
    this.enabled = false;
    console.log('[Profile] Profiler disabled');
  }

  start(name: string, metadata?: Record<string, unknown>): void {
    if (!this.enabled) return;

    this.entries.set(name, {
      name,
      startTime: performance.now(),
      metadata,
    });
  }

  end(name: string): number | undefined {
    if (!this.enabled) return undefined;

    const entry = this.entries.get(name);
    if (!entry) {
      console.warn(`[Profile] No entry found for ${name}`);
      return undefined;
    }

    entry.endTime = performance.now();
    entry.duration = entry.endTime - entry.startTime;
    return entry.duration;
  }

  getStats(): ProfileStats {
    const entries = Array.from(this.entries.values()).filter(e => e.duration !== undefined);
    const totalTime = entries.reduce((sum, e) => sum + (e.duration || 0), 0);

    const summary: Record<string, { count: number; totalDuration: number; avgDuration: number }> = {};

    for (const entry of entries) {
      const key = entry.name;
      if (!summary[key]) {
        summary[key] = { count: 0, totalDuration: 0, avgDuration: 0 };
      }
      summary[key].count++;
      summary[key].totalDuration += entry.duration || 0;
    }

    for (const key of Object.keys(summary)) {
      summary[key].avgDuration = summary[key].totalDuration / summary[key].count;
    }

    return { totalTime, entries, summary };
  }

  printStats(): void {
    const stats = this.getStats();

    console.log('\n=== Profile Results ===\n');
    console.log(`Total time: ${stats.totalTime.toFixed(2)}ms\n`);

    console.log('Summary (by operation):');
    console.log('─'.repeat(70));
    console.log('Operation'.padEnd(40) + 'Count'.padStart(8) + 'Total(ms)'.padStart(12) + 'Avg(ms)'.padStart(12));
    console.log('─'.repeat(70));

    const sortedSummary = Object.entries(stats.summary).sort((a, b) => b[1].totalDuration - a[1].totalDuration);

    for (const [name, data] of sortedSummary) {
      console.log(
        name.padEnd(40) +
        data.count.toString().padStart(8) +
        data.totalDuration.toFixed(2).padStart(12) +
        data.avgDuration.toFixed(2).padStart(12)
      );
    }

    console.log('\n');
  }

  reset(): void {
    this.entries.clear();
  }
}

export const profiler = new Profiler();

export function withProfiling<T>(name: string, fn: () => T): T {
  profiler.start(name);
  try {
    return fn();
  } finally {
    profiler.end(name);
  }
}

export async function withProfilingAsync<T>(name: string, fn: () => Promise<T>): Promise<T> {
  profiler.start(name);
  try {
    return await fn();
  } finally {
    profiler.end(name);
  }
}

export function enableGlobalProfiling(): void {
  profiler.enable();

  const obs = new PerformanceObserver((items) => {
    for (const item of items.getEntries()) {
      if (item.entryType === 'measure') {
        console.log(`[Profile] ${item.name}: ${item.duration.toFixed(2)}ms`);
      }
    }
  });

  obs.observe({ entryTypes: ['measure', 'mark'] });
}

export async function runProfileCommand(args: string[]): Promise<void> {
  const subcommand = args[0] || 'status';

  switch (subcommand) {
    case 'enable':
      profiler.enable();
      break;
    case 'disable':
      profiler.disable();
      break;
    case 'status':
      console.log(`Profiler: ${profiler ? 'enabled' : 'disabled'}`);
      break;
    case 'stats':
      profiler.printStats();
      break;
    case 'reset':
      profiler.reset();
      console.log('Profiler reset');
      break;
    default:
      console.log(`Unknown profile command: ${subcommand}`);
      console.log('Available: enable, disable, status, stats, reset');
  }
}
