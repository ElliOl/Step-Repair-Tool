/**
 * Native C++ addon bridge for StepFixer
 */

import { app } from 'electron'
import path from 'node:path'
import { existsSync } from 'node:fs'

export interface AnalyseResult {
  namesFlagged: number
  shellsSplit: number
  hoopsCompatFixes: number
  shapeId: string
  mesh: MeshPayload
  edges: EdgePayload
  parts: PartNode[]
}

export interface RepairResult {
  success: boolean
  log: string[]
  shapeId: string
  mesh: MeshPayload
  edges: EdgePayload
  parts: PartNode[]
}

export interface MeshPayload {
  positions: Float32Array
  normals: Float32Array
  indices: Uint32Array
  bboxMin: [number, number, number]
  bboxMax: [number, number, number]
  faceCount: number
  triangleCount: number
}

export interface EdgePayload {
  positions: Float32Array
  indices: Uint32Array
  edgeCount: number
}

export interface PartNode {
  id: string
  name: string
  parentId: string | null
  children: string[]
  color: [number, number, number] | null
  shapeType: string
  isAssembly: boolean
  startVertex: number
  vertexCount: number
  startIndex: number
  indexCount: number
  startEdgePoint: number
  edgePointCount: number
  startEdgeIndex: number
  edgeIndexCount: number
}

let addon: any = null
let initError: Error | null = null

function getAddon() {
  if (initError) throw initError
  if (!addon) {
    try {
      let addonPath: string
      if (app.isPackaged) {
        addonPath = path.join(process.resourcesPath, 'app.asar.unpacked', 'native', 'build', 'Release', 'step_fixer_native.node')
      } else {
        addonPath = path.join(__dirname, '../../native/build/Release/step_fixer_native.node')
      }
      if (!existsSync(addonPath)) throw new Error(`Addon not found: ${addonPath}`)
      addon = require(addonPath)
    } catch (e) {
      initError = new Error(`Failed to load native addon: ${e instanceof Error ? e.message : String(e)}`)
      throw initError
    }
  }
  return addon
}

export async function analyseStep(
  filepath: string,
  quality: string = 'fast',
  onLog?: (msg: string) => void,
): Promise<AnalyseResult> {
  const a = getAddon()
  const raw = await a.analyseStep(filepath, quality, onLog ?? null)
  return {
    namesFlagged: raw.namesFlagged,
    shellsSplit: raw.shellsSplit,
    hoopsCompatFixes: raw.hoopsCompatFixes ?? 0,
    shapeId: raw.shapeId,
    mesh: raw.mesh,
    edges: raw.edges,
    parts: raw.parts || [],
  }
}

export async function repairStep(
  filepath: string,
  outputPath: string,
  options: { fixNames: boolean; fixShells: boolean; fixHoopsCompat: boolean },
  onLog: (msg: string) => void
): Promise<RepairResult> {
  const a = getAddon()
  const raw = await a.repairStep(filepath, outputPath, options, onLog)
  return {
    success: raw.success,
    log: raw.log || [],
    shapeId: raw.shapeId,
    mesh: raw.mesh,
    edges: raw.edges,
    parts: raw.parts || [],
  }
}

export async function loadStepMesh(filepath: string, quality: string = 'standard'): Promise<{
  shapeId: string
  mesh: MeshPayload
  edges: EdgePayload
  parts: PartNode[]
}> {
  const a = getAddon()
  const raw = await a.loadStepMesh(filepath, quality)
  return {
    shapeId: raw.shapeId,
    mesh: raw.mesh,
    edges: raw.edges,
    parts: raw.parts || [],
  }
}

export function healthCheck(): boolean {
  try {
    return getAddon().health()
  } catch {
    return false
  }
}

export function getVersion(): string {
  try {
    return getAddon().getVersion()
  } catch {
    return '0.0.0'
  }
}
