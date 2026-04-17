export enum LogLevel {
  ERROR = 0,
  WARN = 1,
  INFO = 2,
  DEBUG = 3,
}

const LOG_LEVEL_NAMES = ['ERROR', 'WARN', 'INFO', 'DEBUG'];

export interface LogEntry {
  timestamp: string;
  level: string;
  message: string;
  correlationId?: string;
  category?: string;
  [key: string]: unknown;
}

export class StructuredLogger {
  private static instance: StructuredLogger;
  private minLevel: LogLevel;
  private enableConsole: boolean;
  private correlationIdCounter: number = 0;

  private constructor() {
    const envLevel = process.env.LOG_LEVEL?.toUpperCase();
    switch (envLevel) {
      case 'ERROR': this.minLevel = LogLevel.ERROR; break;
      case 'WARN': this.minLevel = LogLevel.WARN; break;
      case 'DEBUG': this.minLevel = LogLevel.DEBUG; break;
      default: this.minLevel = LogLevel.INFO;
    }
    this.enableConsole = process.env.LOG_CONSOLE !== 'false';
  }

  static getInstance(): StructuredLogger {
    if (!StructuredLogger.instance) {
      StructuredLogger.instance = new StructuredLogger();
    }
    return StructuredLogger.instance;
  }

  generateCorrelationId(): string {
    return `req-${Date.now()}-${++this.correlationIdCounter}`;
  }

  private shouldLog(level: LogLevel): boolean {
    return level <= this.minLevel;
  }

  private formatEntry(level: LogLevel, message: string, extra: Record<string, unknown> = {}): LogEntry {
    return {
      timestamp: new Date().toISOString(),
      level: LOG_LEVEL_NAMES[level],
      message,
      ...extra,
    };
  }

  private output(entry: LogEntry): void {
    if (this.enableConsole) {
      const prefix = `[${entry.timestamp}] [${entry.level}]`;
      if (entry.correlationId) {
        console.error(`${prefix} [${entry.correlationId}] ${entry.message}`, entry.category ? `{${entry.category}}` : '');
      } else {
        console.error(`${prefix} ${entry.message}`, entry.category ? `{${entry.category}}` : '');
      }
    }
  }

  error(message: string, extra: Record<string, unknown> = {}): void {
    if (!this.shouldLog(LogLevel.ERROR)) return;
    const entry = this.formatEntry(LogLevel.ERROR, message, extra);
    this.output(entry);
  }

  warn(message: string, extra: Record<string, unknown> = {}): void {
    if (!this.shouldLog(LogLevel.WARN)) return;
    const entry = this.formatEntry(LogLevel.WARN, message, extra);
    this.output(entry);
  }

  info(message: string, extra: Record<string, unknown> = {}): void {
    if (!this.shouldLog(LogLevel.INFO)) return;
    const entry = this.formatEntry(LogLevel.INFO, message, extra);
    this.output(entry);
  }

  debug(message: string, extra: Record<string, unknown> = {}): void {
    if (!this.shouldLog(LogLevel.DEBUG)) return;
    const entry = this.formatEntry(LogLevel.DEBUG, message, extra);
    this.output(entry);
  }

  withCorrelationId<T>(fn: (correlationId: string) => T, correlationId?: string): T {
    const id = correlationId || this.generateCorrelationId();
    return fn(id);
  }

  async withLogging<T>(
    operation: string,
    fn: (correlationId: string) => Promise<T>,
    extra: Record<string, unknown> = {}
  ): Promise<T> {
    const correlationId = this.generateCorrelationId();
    this.info(`Starting ${operation}`, { correlationId, ...extra });
    const startTime = Date.now();

    try {
      const result = await fn(correlationId);
      const duration = Date.now() - startTime;
      this.info(`Completed ${operation}`, { correlationId, duration, ...extra });
      return result;
    } catch (error) {
      const duration = Date.now() - startTime;
      this.error(`Failed ${operation}`, {
        correlationId,
        duration,
        error: error instanceof Error ? error.message : String(error),
        ...extra,
      });
      throw error;
    }
  }
}

export const logger = StructuredLogger.getInstance();
