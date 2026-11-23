import { z } from 'zod';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { format } from 'prettier';
import { documentSchema } from '../src/core/model';
type Schema = {
  type?: string;
  const?: unknown;
  enum?: unknown[];
  anyOf?: Schema[];
  oneOf?: Schema[];
  allOf?: Schema[];
  properties?: Record<string, Schema>;
  required?: string[];
  additionalProperties?: boolean | Schema;
  items?: Schema;
  prefixItems?: Schema[];
  $ref?: string;
};
function ts(s: Schema): string {
  if (s.$ref) throw new Error('Unexpected JSON Schema reference: ' + s.$ref);
  if ('const' in s) return JSON.stringify(s.const);
  if (s.enum) return s.enum.map((v) => JSON.stringify(v)).join(' | ');
  if (s.anyOf || s.oneOf) return (s.anyOf ?? s.oneOf)!.map(ts).join(' | ');
  if (s.allOf) return s.allOf.map(ts).join(' & ');
  switch (s.type) {
    case 'string':
      return 'string';
    case 'number':
    case 'integer':
      return 'number';
    case 'boolean':
      return 'boolean';
    case 'null':
      return 'null';
    case 'array':
      return s.prefixItems
        ? '[' + s.prefixItems.map(ts).join(',') + ']'
        : 'Array<' + (s.items ? ts(s.items) : 'unknown') + '>';
    case 'object': {
      const props = Object.entries(s.properties ?? {}).map(
        ([key, value]) =>
          JSON.stringify(key) + (s.required?.includes(key) ? '' : '?') + ':' + ts(value) + ';',
      );
      if (s.additionalProperties && typeof s.additionalProperties === 'object')
        props.push('[key:string]:' + ts(s.additionalProperties) + ';');
      return '{' + props.join('\n') + '}';
    }
    default:
      if (Object.keys(s).every((k) => k === '$schema' || k === 'description' || k === 'title'))
        return 'unknown';
      throw new Error('Unsupported schema shape ' + JSON.stringify(s));
  }
}
const schema = z.toJSONSchema(documentSchema, { io: 'output', reused: 'inline' }) as Schema;
const template = await readFile('scripts/runtime-api-template.txt', 'utf8');
const source =
  "// Generated from the runtime schema. No Zod dependency is needed by game consumers.\nimport type * as THREE from 'three';\nexport type EffectDocument=" +
  ts(schema) +
  ';\n' +
  template;
await mkdir('public/runtime', { recursive: true });
await writeFile(
  'public/runtime/vfx-runtime.d.ts',
  await format(source, { parser: 'typescript', singleQuote: true, printWidth: 100 }),
);
