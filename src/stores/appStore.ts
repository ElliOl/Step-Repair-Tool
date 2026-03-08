import { create } from 'zustand'
import type { FileEntry, RepairOptions, ViewerModel } from '../types'
import { getAllDescendantIds } from '../utils/partTreeUtils'

interface AppState {
  files: FileEntry[]
  options: RepairOptions
  log: string[]
  model: ViewerModel | null
  currentFileName: string | null
  loading: boolean
  loadingStage: string
  loadingLogs: string[]
  loadingProgress: number
  // Viewer display settings
  showEdges: boolean
  meshQuality: 'fast' | 'standard' | 'fine'
  upAxis: '+Y' | '+Z'
  // Part tree UI
  showPartTree: boolean
  partVisibility: Record<string, boolean>
  selectedPartIds: Set<string>
  hoveredPartId: string | null
  // Actions
  addFiles: (paths: string[]) => void
  removeFile: (filepath: string) => void
  setFileStatus: (filepath: string, status: FileEntry['status'], payload?: Partial<FileEntry>) => void
  setOptions: (opts: Partial<RepairOptions>) => void
  appendLog: (line: string) => void
  clearLog: () => void
  setModel: (model: ViewerModel | null, fileName?: string | null) => void
  setLoading: (v: boolean) => void
  setLoadingStage: (stage: string) => void
  addLoadingLog: (line: string) => void
  clearLoadingState: () => void
  setLoadingProgress: (progress: number) => void
  setShowEdges: (v: boolean) => void
  setMeshQuality: (q: 'fast' | 'standard' | 'fine') => void
  setUpAxis: (axis: '+Y' | '+Z') => void
  // Part tree actions
  togglePartTree: () => void
  setShowPartTree: (v: boolean) => void
  setPartVisibility: (nodeId: string, visible: boolean) => void
  selectPart: (nodeId: string, mode: 'replace' | 'add' | 'subtract') => void
  selectAllParts: (ids: string[]) => void
  setHoveredPart: (id: string | null) => void
}

export const useAppStore = create<AppState>((set, get) => ({
  files: [],
  options: { fixNames: true, fixShells: true, fixHoopsCompat: true },
  log: [],
  model: null,
  currentFileName: null,
  loading: false,
  loadingStage: '',
  loadingLogs: [],
  loadingProgress: 0,
  showEdges: true,
  meshQuality: 'fast',
  upAxis: '+Y',
  showPartTree: false,
  partVisibility: {},
  selectedPartIds: new Set(),
  hoveredPartId: null,

  addFiles: (paths) =>
    set((state) => {
      // Single file only: new upload replaces the old one
      const path = paths[0]
      if (!path) return state
      const file = {
        filepath: path,
        name: path.split(/[/\\]/).pop() ?? path,
        status: 'idle' as const,
      }
      return {
        files: [file],
        model: null,
        currentFileName: null,
      }
    }),

  removeFile: (filepath) =>
    set((state) => ({ files: state.files.filter((f) => f.filepath !== filepath) })),

  setFileStatus: (filepath, status, payload) =>
    set((state) => ({
      files: state.files.map((f) =>
        f.filepath === filepath ? { ...f, status, ...payload } : f,
      ),
    })),

  setOptions: (opts) =>
    set((state) => ({ options: { ...state.options, ...opts } })),

  appendLog: (line) =>
    set((state) => ({ log: [...state.log, line] })),

  clearLog: () => set({ log: [] }),

  setModel: (model, fileName) =>
    set({
      model,
      currentFileName: fileName ?? null,
      // Reset part tree interaction state when a new model is loaded
      selectedPartIds: new Set(),
      hoveredPartId: null,
      partVisibility: model
        ? Object.fromEntries(model.parts.map((p) => [p.id, true]))
        : {},
    }),

  setLoading: (v) => set({ loading: v }),

  setLoadingStage: (stage) => set({ loadingStage: stage }),

  addLoadingLog: (line) =>
    set((state) => ({ loadingLogs: [...state.loadingLogs, line] })),

  clearLoadingState: () => set({ loadingLogs: [], loadingProgress: 0, loadingStage: '' }),

  setLoadingProgress: (progress) => set({ loadingProgress: progress }),

  setShowEdges: (v) => set({ showEdges: v }),

  setMeshQuality: (q) => set({ meshQuality: q }),

  setUpAxis: (axis) => set({ upAxis: axis }),

  togglePartTree: () => set((state) => ({ showPartTree: !state.showPartTree })),

  setShowPartTree: (v) => set({ showPartTree: v }),

  setPartVisibility: (nodeId, visible) =>
    set((state) => {
      const parts = state.model?.parts ?? []
      // Cascade visibility to all descendants
      const ids = getAllDescendantIds(nodeId, parts)
      const updated = { ...state.partVisibility }
      ids.forEach((id) => {
        updated[id] = visible
      })
      return { partVisibility: updated }
    }),

  selectPart: (nodeId, mode) =>
    set((state) => {
      const next = new Set(state.selectedPartIds)
      if (mode === 'replace') {
        next.clear()
        next.add(nodeId)
      } else if (mode === 'add') {
        next.add(nodeId)
      } else {
        next.delete(nodeId)
      }
      return { selectedPartIds: next }
    }),

  selectAllParts: (ids) =>
    set((state) => {
      const next = new Set(state.selectedPartIds)
      ids.forEach((id) => next.add(id))
      return { selectedPartIds: next }
    }),

  setHoveredPart: (id) => set({ hoveredPartId: id }),
}))
