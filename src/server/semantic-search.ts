import type { MCPToolDefinition } from '../server-types.js';
import { toolCache } from './cache.js';

interface ToolEmbedding {
  name: string;
  embedding: number[];
  description: string;
  keywords: string[];
}

const TOOL_EMBEDDINGS: ToolEmbedding[] = [
  { name: 'launch_editor', embedding: [0.1, 0.2], description: 'Opens Godot editor GUI', keywords: ['open', 'launch', 'editor', 'gui'] },
  { name: 'run_project', embedding: [0.15, 0.25], description: 'Runs Godot project', keywords: ['run', 'execute', 'start', 'game'] },
  { name: 'create_scene', embedding: [0.3, 0.4], description: 'Creates new scene', keywords: ['create', 'new', 'scene'] },
  { name: 'add_node', embedding: [0.35, 0.45], description: 'Adds node to scene', keywords: ['add', 'node', 'scene', 'insert'] },
  { name: 'delete_node', embedding: [0.4, 0.3], description: 'Deletes node', keywords: ['delete', 'remove', 'node'] },
  { name: 'create_script', embedding: [0.5, 0.6], description: 'Creates GDScript file', keywords: ['create', 'script', 'gdscript', 'code'] },
  { name: 'modify_script', embedding: [0.55, 0.65], description: 'Modifies script content', keywords: ['modify', 'edit', 'script', 'change'] },
  { name: 'set_breakpoint', embedding: [0.7, 0.8], description: 'Sets debugger breakpoint', keywords: ['debug', 'breakpoint', 'stop'] },
  { name: 'get_debug_output', embedding: [0.2, 0.3], description: 'Gets console output', keywords: ['debug', 'output', 'console', 'log'] },
  { name: 'capture_screenshot', embedding: [0.25, 0.35], description: 'Takes screenshot', keywords: ['screenshot', 'capture', 'image'] },
];

function cosineSimilarity(a: number[], b: number[]): number {
  if (a.length !== b.length) return 0;
  let dot = 0;
  let normA = 0;
  let normB = 0;
  for (let i = 0; i < a.length; i++) {
    dot += a[i] * b[i];
    normA += a[i] * a[i];
    normB += b[i] * b[i];
  }
  return dot / (Math.sqrt(normA) * Math.sqrt(normB));
}

function simpleTextEmbedding(text: string): number[] {
  const words = text.toLowerCase().split(/\s+/);
  const embedding = new Array(10).fill(0);
  for (const word of words) {
    const hash = simpleHash(word);
    embedding[hash % 10] += 1;
  }
  const norm = Math.sqrt(embedding.reduce((sum, v) => sum + v * v, 0));
  return embedding.map(v => v / (norm || 1));
}

function simpleHash(str: string): number {
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    const char = str.charCodeAt(i);
    hash = ((hash << 5) - hash) + char;
    hash = hash & hash;
  }
  return Math.abs(hash);
}

export function semanticToolSearch(query: string, tools: MCPToolDefinition[], limit: number = 10): MCPToolDefinition[] {
  const queryEmbedding = simpleTextEmbedding(query);
  
  const scoredTools = tools.map(tool => {
    const toolText = `${tool.name} ${tool.description}`.toLowerCase();
    const toolEmbedding = simpleTextEmbedding(toolText);
    const score = cosineSimilarity(queryEmbedding, toolEmbedding);
    
    const keywordBonus = tool.description.toLowerCase().includes(query.toLowerCase()) ? 0.1 : 0;
    return { tool, score: score + keywordBonus };
  });
  
  scoredTools.sort((a, b) => b.score - a.score);
  
  return scoredTools.slice(0, limit).map(s => s.tool);
}

export function invalidateToolCache(): void {
  toolCache.invalidate();
}

export function getCachedTools(): MCPToolDefinition[] | null {
  return toolCache.get();
}

export function setCachedTools(tools: MCPToolDefinition[], ttl?: number): void {
  toolCache.set(tools, ttl);
}
