export interface CircuitBreakerConfig {
  failureThreshold: number;
  successThreshold: number;
  timeout: number;
  resetTimeout: number;
}

export type CircuitState = 'closed' | 'open' | 'half-open';

export interface CircuitBreakerStats {
  state: CircuitState;
  failures: number;
  successes: number;
  lastFailure: string | null;
  nextResetTime: number | null;
}

export class CircuitBreaker {
  private state: CircuitState = 'closed';
  private failures: number = 0;
  private successes: number = 0;
  private nextResetTime: number | null = null;
  private readonly config: Required<CircuitBreakerConfig>;

  constructor(config: Partial<CircuitBreakerConfig> = {}) {
    this.config = {
      failureThreshold: config.failureThreshold ?? 5,
      successThreshold: config.successThreshold ?? 3,
      timeout: config.timeout ?? 60000,
      resetTimeout: config.resetTimeout ?? 30000,
    };
  }

  getState(): CircuitState {
    if (this.state === 'open' && this.nextResetTime !== null) {
      if (Date.now() >= this.nextResetTime) {
        this.state = 'half-open';
        this.nextResetTime = null;
      }
    }
    return this.state;
  }

  getStats(): CircuitBreakerStats {
    return {
      state: this.getState(),
      failures: this.failures,
      successes: this.successes,
      lastFailure: this.lastFailure ?? null,
      nextResetTime: this.nextResetTime,
    };
  }

  private lastFailure: string | null = null;

  async execute<T>(operation: () => Promise<T>, fallback?: () => Promise<T>): Promise<T> {
    const currentState = this.getState();

    if (currentState === 'open') {
      if (fallback) {
        return fallback();
      }
      throw new Error(`Circuit breaker is open. Try again in ${this.getRetryAfter()}ms`);
    }

    try {
      const result = await operation();
      this.onSuccess();
      return result;
    } catch (error) {
      this.onFailure(error instanceof Error ? error.message : String(error));
      if (fallback) {
        return fallback();
      }
      throw error;
    }
  }

  private onSuccess(): void {
    this.failures = 0;
    this.lastFailure = null;

    if (this.state === 'half-open') {
      this.successes++;
      if (this.successes >= this.config.successThreshold) {
        this.state = 'closed';
        this.successes = 0;
      }
    }
  }

  private onFailure(reason: string): void {
    this.failures++;
    this.lastFailure = reason;

    if (this.state === 'closed') {
      if (this.failures >= this.config.failureThreshold) {
        this.state = 'open';
        this.nextResetTime = Date.now() + this.config.resetTimeout;
      }
    } else if (this.state === 'half-open') {
      this.state = 'open';
      this.nextResetTime = Date.now() + this.config.resetTimeout;
      this.successes = 0;
    }
  }

  getRetryAfter(): number {
    if (this.state === 'open' && this.nextResetTime !== null) {
      return Math.max(0, this.nextResetTime - Date.now());
    }
    return 0;
  }

  reset(): void {
    this.state = 'closed';
    this.failures = 0;
    this.successes = 0;
    this.lastFailure = null;
    this.nextResetTime = null;
  }
}

export interface RetryConfig {
  maxAttempts: number;
  initialDelay: number;
  maxDelay: number;
  backoffMultiplier: number;
  retryableErrors?: RegExp[];
}

export class RetryHandler {
  private config: Required<RetryConfig>;

  constructor(config: Partial<RetryConfig> = {}) {
    this.config = {
      maxAttempts: config.maxAttempts ?? 3,
      initialDelay: config.initialDelay ?? 1000,
      maxDelay: config.maxDelay ?? 30000,
      backoffMultiplier: config.backoffMultiplier ?? 2,
      retryableErrors: config.retryableErrors ?? [
        /ECONNREFUSED/i,
        /ETIMEDOUT/i,
        /ENOTFOUND/i,
        /socket hang up/i,
        /敷途超时/i,
      ],
    };
  }

  async execute<T>(
    operation: () => Promise<T>,
    onRetry?: (attempt: number, error: Error, delay: number) => void
  ): Promise<T> {
    let lastError: Error | null = null;
    let delay = this.config.initialDelay;

    for (let attempt = 1; attempt <= this.config.maxAttempts; attempt++) {
      try {
        return await operation();
      } catch (error) {
        lastError = error instanceof Error ? error : new Error(String(error));

        if (attempt >= this.config.maxAttempts) {
          break;
        }

        const isRetryable = this.config.retryableErrors.some((regex) =>
          regex.test(lastError!.message)
        );

        if (!isRetryable) {
          throw lastError;
        }

        if (onRetry) {
          onRetry(attempt, lastError, delay);
        }

        await this.sleep(delay);
        delay = Math.min(delay * this.config.backoffMultiplier, this.config.maxDelay);
      }
    }

    throw lastError;
  }

  private sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }
}
