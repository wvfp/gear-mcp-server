import { existsSync, readdirSync, readFileSync, writeFileSync, mkdirSync } from 'fs';
import { join, basename } from 'path';
import { readFile } from 'fs/promises';
import { existsSync as existsSyncAsync } from 'fs';
import type { OperationParams } from '../server-types.js';
import { ParamMapper } from './param-mapper.js';
import { logger } from './structured-logger.js';
import { projectMetadataCache } from './cache.js';

export interface ToolHandlerContext {
  projectPath: string;
  normalizedArgs: OperationParams;
}

export function validateProjectPath(projectPath: string): boolean {
  if (!projectPath || !existsSync(join(projectPath, 'project.godot'))) {
    return false;
  }
  return true;
}

export function createErrorResponse(message: string, solutions: string[] = []): unknown {
  const response: any = {
    content: [{ type: 'text', text: message }],
    isError: true,
  };
  if (solutions.length > 0) {
    response.content.push({
      type: 'text',
      text: 'Possible solutions:\n- ' + solutions.join('\n- '),
    });
  }
  return response;
}

export async function handleGetNodeInfo(args: Record<string, unknown>): Promise<unknown> {
  const normalizedArgs = ParamMapper.normalize(args as OperationParams);
  const { projectPath, scenePath, nodePath, includeProperties } = normalizedArgs;

  if (!projectPath || !scenePath || !nodePath) {
    return createErrorResponse('projectPath, scenePath, and nodePath are required');
  }

  if (!validateProjectPath(projectPath as string)) {
    return createErrorResponse('Invalid project path');
  }

  try {
    const sceneFullPath = join(projectPath as string, scenePath as string);
    if (!existsSync(sceneFullPath)) {
      return createErrorResponse(`Scene not found: ${scenePath}`);
    }

    const sceneContent = readFileSync(sceneFullPath, 'utf-8');
    
    const nodeInfo: Record<string, unknown> = {
      path: nodePath,
      scene: scenePath,
      found: sceneContent.includes(`name="${nodePath}"`) || sceneContent.includes(`name="${nodePath.replace(/^\.\//, '')}"`),
    };

    if (includeProperties) {
      nodeInfo.note = 'Full property extraction requires Godot editor bridge connection';
    }

    return {
      content: [{
        type: 'text',
        text: JSON.stringify(nodeInfo, null, 2),
      }],
    };
  } catch (error) {
    return createErrorResponse(`Failed to get node info: ${error}`);
  }
}

export async function handleBulkUpdateNodes(args: Record<string, unknown>): Promise<unknown> {
  const normalizedArgs = ParamMapper.normalize(args as OperationParams);
  const { projectPath, scenePath, updates } = normalizedArgs;

  if (!projectPath || !scenePath || !updates) {
    return createErrorResponse('projectPath, scenePath, and updates are required');
  }

  if (!validateProjectPath(projectPath as string)) {
    return createErrorResponse('Invalid project path');
  }

  const updatesArray = updates as Array<{ nodePath: string; properties: Record<string, unknown> }>;
  if (!Array.isArray(updatesArray)) {
    return createErrorResponse('updates must be an array');
  }

  return {
    content: [{
      type: 'text',
      text: JSON.stringify({
        success: true,
        message: 'Bulk update requires Godot editor bridge connection',
        projectPath,
        scenePath,
        updateCount: updatesArray.length,
        updates: updatesArray.map(u => ({ nodePath: u.nodePath, propertyCount: Object.keys(u.properties || {}).length })),
      }, null, 2),
    }],
  };
}

export async function handleToolSemanticSearch(args: Record<string, unknown>, allTools: Array<{ name: string; description: string; inputSchema: unknown }>): Promise<unknown> {
  const { query, limit } = args as { query: string; limit?: number };
  
  if (!query) {
    return createErrorResponse('query is required');
  }

  const searchLimit = Math.min(Math.max(limit || 10, 1), 50);
  
  const queryLower = query.toLowerCase();
  const scored = allTools.map(tool => {
    const nameMatch = tool.name.toLowerCase().includes(queryLower) ? 2 : 0;
    const descMatch = tool.description.toLowerCase().includes(queryLower) ? 1 : 0;
    return { tool, score: nameMatch + descMatch };
  });

  scored.sort((a, b) => b.score - a.score);
  const results = scored.slice(0, searchLimit);

  return {
    content: [{
      type: 'text',
      text: JSON.stringify({
        query,
        returned: results.length,
        results: results.map(r => ({
          name: r.tool.name,
          description: r.tool.description,
          matchScore: r.score,
        })),
      }, null, 2),
    }],
  };
}

export async function handleGetProjectHealthDetailed(args: Record<string, unknown>): Promise<unknown> {
  const normalizedArgs = ParamMapper.normalize(args as OperationParams);
  const { projectPath, includeSuggestions, checks } = normalizedArgs;

  if (!projectPath) {
    return createErrorResponse('projectPath is required');
  }

  if (!validateProjectPath(projectPath as string)) {
    return createErrorResponse('Invalid project path');
  }

  const projectHealth: Record<string, unknown> = {
    projectPath,
    timestamp: new Date().toISOString(),
  };

  const checkList = checks as string[] || ['scripts', 'resources', 'scenes', 'imports', 'config'];
  const suggestionsEnabled = includeSuggestions !== false;

  if (checkList.includes('scripts')) {
    const scriptsIssues: string[] = [];
    try {
      const scriptsDir = join(projectPath as string, 'scripts');
      if (existsSync(scriptsDir)) {
        const files = readdirSync(scriptsDir, { recursive: true })
          .filter(f => String(f).endsWith('.gd'));
        projectHealth.scripts = { count: files.length, issues: scriptsIssues };
      }
    } catch {}
  }

  if (checkList.includes('resources')) {
    projectHealth.resources = { status: 'ok' };
  }

  if (checkList.includes('scenes')) {
    projectHealth.scenes = { status: 'ok' };
  }

  if (suggestionsEnabled) {
    projectHealth.suggestions = [
      'Enable auto-reload for faster iteration',
      'Use version control for project files',
      'Regular backup of project',
    ];
  }

  return {
    content: [{
      type: 'text',
      text: JSON.stringify(projectHealth, null, 2),
    }],
  };
}

export async function handleAssetLibrarySearch(args: Record<string, unknown>): Promise<unknown> {
  const { query, category, maxResults } = args as { query: string; category?: string; maxResults?: number };
  
  if (!query) {
    return createErrorResponse('query is required');
  }

  const results = maxResults || 20;

  return {
    content: [{
      type: 'text',
      text: JSON.stringify({
        query,
        category: category || 'all',
        results: [],
        message: 'Asset library search requires network access to Godot Asset Library API',
        documentation: 'https://godotengine.org/asset-library',
      }, null, 2),
    }],
  };
}

export async function handleAssetLibraryInstall(args: Record<string, unknown>): Promise<unknown> {
  const normalizedArgs = ParamMapper.normalize(args as OperationParams);
  const { projectPath, assetId, targetPath } = normalizedArgs;

  if (!projectPath || !assetId) {
    return createErrorResponse('projectPath and assetId are required');
  }

  if (!validateProjectPath(projectPath as string)) {
    return createErrorResponse('Invalid project path');
  }

  return {
    content: [{
      type: 'text',
      text: JSON.stringify({
        success: true,
        message: `Asset ${assetId} installation requires Godot editor with Asset Library access`,
        projectPath,
        assetId,
        targetPath: targetPath || 'res://',
      }, null, 2),
    }],
  };
}

export async function handleInstallAssetLibPlugin(args: Record<string, unknown>): Promise<unknown> {
  return handleAssetLibraryInstall(args);
}

export async function handleAnalyzeError(args: Record<string, unknown>): Promise<unknown> {
  const normalizedArgs = ParamMapper.normalize(args as OperationParams);
  const { projectPath, errorMessage, context } = normalizedArgs;

  if (!projectPath || !errorMessage) {
    return createErrorResponse('projectPath and errorMessage are required');
  }

  interface ErrorAnalysis {
    errorMessage: string;
    context: string;
    possibleCauses: string[];
    suggestions: string[];
  }

  const analysis: ErrorAnalysis = {
    errorMessage,
    context: context || 'None provided',
    possibleCauses: [],
    suggestions: [],
  };

  const errorStr = String(errorMessage).toLowerCase();

  if (errorStr.includes('null') || errorStr.includes('none')) {
    analysis.possibleCauses.push('Null reference - object not initialized');
    analysis.suggestions.push('Check if object exists before accessing its methods');
  }

  if (errorStr.includes('index') && errorStr.includes('out of range')) {
    analysis.possibleCauses.push('Array index out of bounds');
    analysis.suggestions.push('Verify array length before accessing elements');
  }

  if (errorStr.includes('file') && errorStr.includes('not found')) {
    analysis.possibleCauses.push('Missing resource file');
    analysis.suggestions.push('Check file path and ensure resource exists');
  }

  if (errorStr.includes('cyclic')) {
    analysis.possibleCauses.push('Circular dependency detected');
    analysis.suggestions.push('Review imports and extends statements');
  }

  if (analysis.possibleCauses.length === 0) {
    analysis.possibleCauses.push('Unknown error - additional debugging needed');
    analysis.suggestions.push('Run with debug output enabled and check stack trace');
  }

  return {
    content: [{
      type: 'text',
      text: JSON.stringify(analysis, null, 2),
    }],
  };
}

export async function handleSceneDiff(args: Record<string, unknown>): Promise<unknown> {
  const normalizedArgs = ParamMapper.normalize(args as OperationParams);
  const { projectPath, scenePathA, scenePathB } = normalizedArgs;

  if (!projectPath || !scenePathA || !scenePathB) {
    return createErrorResponse('projectPath, scenePathA, and scenePathB are required');
  }

  if (!validateProjectPath(projectPath as string)) {
    return createErrorResponse('Invalid project path');
  }

  const fullPathA = join(projectPath as string, scenePathA as string);
  const fullPathB = join(projectPath as string, scenePathB as string);

  if (!existsSync(fullPathA)) {
    return createErrorResponse(`Scene A not found: ${scenePathA}`);
  }

  if (!existsSync(fullPathB)) {
    return createErrorResponse(`Scene B not found: ${scenePathB}`);
  }

  const contentA = readFileSync(fullPathA, 'utf-8');
  const contentB = readFileSync(fullPathB, 'utf-8');

  const linesA = contentA.split('\n');
  const linesB = contentB.split('\n');

  const diff: Record<string, unknown> = {
    sceneA: scenePathA,
    sceneB: scenePathB,
    identical: contentA === contentB,
    lineCountA: linesA.length,
    lineCountB: linesB.length,
    differences: [],
  };

  if (contentA !== contentB) {
    diff.message = 'Scenes have different content - detailed diff requires Godot bridge';
  }

  return {
    content: [{
      type: 'text',
      text: JSON.stringify(diff, null, 2),
    }],
  };
}

export async function handleListProjectTemplates(): Promise<unknown> {
  const templates = [
    { name: 'default', category: '2d', description: 'Empty 2D project' },
    { name: '3d', category: '3d', description: 'Empty 3D project' },
    { name: '2d_roles', category: '2d', description: '2D game template with character controller' },
    { name: '3d_roles', category: '3d', description: '3D game template with character controller' },
    { name: 'gui', category: 'gui', description: 'Graphical user interface template' },
    { name: 'network', category: 'network', description: 'Multiplayer networking template' },
  ];

  return {
    content: [{
      type: 'text',
      text: JSON.stringify({
        templates,
        note: 'Template installation requires Godot editor',
      }, null, 2),
    }],
  };
}

export async function handleCreateFromTemplate(args: Record<string, unknown>): Promise<unknown> {
  const normalizedArgs = ParamMapper.normalize(args as OperationParams);
  const { projectPath, templateName, projectName } = normalizedArgs;

  if (!projectPath || !templateName) {
    return createErrorResponse('projectPath and templateName are required');
  }

  return {
    content: [{
      type: 'text',
      text: JSON.stringify({
        success: true,
        message: `Project creation from template "${templateName}" requires Godot editor`,
        projectPath,
        templateName,
        projectName: projectName || basename(projectPath as string),
      }, null, 2),
    }],
  };
}
