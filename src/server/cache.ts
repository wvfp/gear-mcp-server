import type { MCPToolDefinition } from '../server-types.js';

export interface ToolDefinitionCache {
  tools: MCPToolDefinition[];
  buildTime: number;
  ttl: number;
}

export class ToolCache {
  private static instance: ToolCache;
  private cache: ToolDefinitionCache | null = null;
  private readonly defaultTTL: number = 60000;

  static getInstance(): ToolCache {
    if (!ToolCache.instance) {
      ToolCache.instance = new ToolCache();
    }
    return ToolCache.instance;
  }

  set(tools: MCPToolDefinition[], ttl?: number): void {
    this.cache = {
      tools,
      buildTime: Date.now(),
      ttl: ttl ?? this.defaultTTL,
    };
  }

  get(): MCPToolDefinition[] | null {
    if (!this.cache) {
      return null;
    }

    if (Date.now() - this.cache.buildTime > this.cache.ttl) {
      this.cache = null;
      return null;
    }

    return this.cache.tools;
  }

  invalidate(): void {
    this.cache = null;
  }

  isValid(): boolean {
    if (!this.cache) {
      return false;
    }
    return Date.now() - this.cache.buildTime <= this.cache.ttl;
  }
}

export interface ProjectMetadataCacheEntry {
  data: unknown;
  buildTime: number;
  ttl: number;
}

export class ProjectMetadataCache {
  private static instance: ProjectMetadataCache;
  private cache: Map<string, ProjectMetadataCacheEntry> = new Map();
  private readonly defaultTTL: number = 300000;

  static getInstance(): ProjectMetadataCache {
    if (!ProjectMetadataCache.instance) {
      ProjectMetadataCache.instance = new ProjectMetadataCache();
    }
    return ProjectMetadataCache.instance;
  }

  set(key: string, data: unknown, ttl?: number): void {
    this.cache.set(key, {
      data,
      buildTime: Date.now(),
      ttl: ttl ?? this.defaultTTL,
    });
  }

  get(key: string): unknown | null {
    const entry = this.cache.get(key);
    if (!entry) {
      return null;
    }

    if (Date.now() - entry.buildTime > entry.ttl) {
      this.cache.delete(key);
      return null;
    }

    return entry.data;
  }

  invalidate(key?: string): void {
    if (key) {
      this.cache.delete(key);
    } else {
      this.cache.clear();
    }
  }

  invalidatePrefix(prefix: string): void {
    for (const key of this.cache.keys()) {
      if (key.startsWith(prefix)) {
        this.cache.delete(key);
      }
    }
  }

  size(): number {
    return this.cache.size;
  }
}

export const toolCache = ToolCache.getInstance();
export const projectMetadataCache = ProjectMetadataCache.getInstance();
