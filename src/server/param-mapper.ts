import type { OperationParams } from '../server-types.js';

export class ParamMapper {
  private static readonly mappings: Record<string, string> = {
    'project_path': 'projectPath',
    'scene_path': 'scenePath',
    'root_node_type': 'rootNodeType',
    'root_type': 'rootNodeType',
    'parent_node_path': 'parentNodePath',
    'node_type': 'nodeType',
    'node_name': 'nodeName',
    'texture_path': 'texturePath',
    'node_path': 'nodePath',
    'output_path': 'outputPath',
    'mesh_item_names': 'meshItemNames',
    'new_path': 'newPath',
    'file_path': 'filePath',
    'directory': 'directory',
    'recursive': 'recursive',
    'scene': 'scene',
    'source_node_path': 'sourceNodePath',
    'signal_name': 'signalName',
    'target_node_path': 'targetNodePath',
    'method_name': 'methodName',
    'player_node_path': 'playerNodePath',
    'animation_name': 'animationName',
    'loop_mode': 'loopMode',
    'plugin_name': 'pluginName',
    'action_name': 'actionName',
    'file_types': 'fileTypes',
    'case_sensitive': 'caseSensitive',
    'max_results': 'maxResults',
    'axis_value': 'axisValue',
    'tileset_path': 'tilesetPath',
    'tile_size': 'tileSize',
    'tilemap_node_path': 'tilemapNodePath',
    'source_id': 'sourceId',
    'atlas_coords': 'atlasCoords',
    'alternative_tile': 'alternativeTile',
  };

  private static readonly reverseMappings: Record<string, string> = {} as Record<string, string>;

  static {
    for (const [snake, camel] of Object.entries(this.mappings)) {
      this.reverseMappings[camel] = snake;
    }
  }

  static normalize(params: OperationParams): OperationParams {
    if (!params || typeof params !== 'object') {
      return params;
    }

    const result: OperationParams = {};

    for (const key in params) {
      if (Object.prototype.hasOwnProperty.call(params, key)) {
        let normalizedKey = key;

        if (key.startsWith('_')) {
          normalizedKey = key;
        } else if (key.includes('_')) {
          normalizedKey = this.mappings[key] || key.replace(/_([a-zA-Z0-9])/g, (_, letter: string) => letter.toUpperCase());
        }

        if (typeof params[key] === 'object' && params[key] !== null && !Array.isArray(params[key])) {
          result[normalizedKey] = this.normalize(params[key] as OperationParams);
        } else {
          result[normalizedKey] = params[key];
        }
      }
    }

    return result;
  }

  static toSnakeCase(params: OperationParams): OperationParams {
    const result: OperationParams = {};

    for (const key in params) {
      if (Object.prototype.hasOwnProperty.call(params, key)) {
        const snakeKey = key.startsWith('_')
          ? key
          : (this.reverseMappings[key] || key.replace(/[A-Z]/g, letter => `_${letter.toLowerCase()}`));

        if (typeof params[key] === 'object' && params[key] !== null && !Array.isArray(params[key])) {
          result[snakeKey] = this.toSnakeCase(params[key] as OperationParams);
        } else {
          result[snakeKey] = params[key];
        }
      }
    }

    return result;
  }
}
