import * as React from 'react'
import { useAppStore } from '../stores/appStore'
import type { FileEntry, RepairOptions } from '../types'


export function useRepairActions() {
  const addFiles = useAppStore((s) => s.addFiles)
  const setFileStatus = useAppStore((s) => s.setFileStatus)
  const setModel = useAppStore((s) => s.setModel)
  const appendLog = useAppStore((s) => s.appendLog)
  const clearLog = useAppStore((s) => s.clearLog)
  const setLoading = useAppStore((s) => s.setLoading)
  const setLoadingStage = useAppStore((s) => s.setLoadingStage)
  const addLoadingLog = useAppStore((s) => s.addLoadingLog)
  const clearLoadingState = useAppStore((s) => s.clearLoadingState)
  const setLoadingProgress = useAppStore((s) => s.setLoadingProgress)

  const analyseFile = React.useCallback(
    async (filepath: string) => {
      const quality = useAppStore.getState().meshQuality
      const fileName = filepath.split(/[/\\]/).pop() ?? filepath

      clearLoadingState()
      setLoadingStage(`Loading ${fileName}`)
      addLoadingLog(`Reading STEP file…`)
      setLoading(true)
      setFileStatus(filepath, 'analysing')

      try {
        const result = await window.electronAPI.analyseStep(filepath, quality)
        setFileStatus(filepath, 'ready', {
          namesFlagged: result.namesFlagged,
          shellsSplit: result.shellsSplit,
          hoopsCompatFixes: result.hoopsCompatFixes,
        })
        setModel(
          {
            shapeId: result.shapeId,
            mesh: result.mesh,
            edges: result.edges,
            parts: result.parts,
          },
          fileName,
        )
      } catch (e) {
        setFileStatus(filepath, 'error', {
          error: e instanceof Error ? e.message : String(e),
        })
        appendLog(
          `[${new Date().toLocaleTimeString()}] Error analysing ${fileName}: ${e instanceof Error ? e.message : String(e)}`,
        )
      } finally {
        setLoading(false)
      }
    },
    [setFileStatus, setModel, appendLog, setLoading, setLoadingStage, addLoadingLog, clearLoadingState],
  )

  const handleBrowse = React.useCallback(async () => {
    const paths = await window.electronAPI.openFileDialog()
    if (paths.length) {
      const path = paths[0]
      addFiles([path])
      await analyseFile(path)
    }
  }, [addFiles, analyseFile])

  const handleRepairOne = React.useCallback(
    async (filepath: string, options: RepairOptions, name: string) => {
      const quality = useAppStore.getState().meshQuality

      clearLoadingState()
      setLoadingStage(`Repairing ${name}`)
      setLoading(true)
      addLoadingLog('Reading file…')
      addLoadingLog('Large files may take several minutes. Progress will appear as processing continues.')
      setFileStatus(filepath, 'repairing')
      appendLog(`[${new Date().toLocaleTimeString()}] ${name} → repairing…`)

      try {
        const dir = filepath.replace(/[/\\][^/\\]+$/, '')
        const base = (name.replace(/\.(stp|step)$/i, '') || 'repaired') + '_fixed.stp'
        const outputPath = `${dir}/${base}`
        const result = await window.electronAPI.repairStep(filepath, outputPath, options)
        if (result.success) {
          setFileStatus(filepath, 'done')
          setModel(
            {
              shapeId: result.shapeId,
              mesh: result.mesh,
              edges: result.edges,
              parts: result.parts,
            },
            base,
          )
          appendLog(`[${new Date().toLocaleTimeString()}] ✓ Saved: ${base}`)
        }
      } catch (e) {
        setFileStatus(filepath, 'error', {
          error: e instanceof Error ? e.message : String(e),
        })
        appendLog(
          `[${new Date().toLocaleTimeString()}] ✗ Error: ${e instanceof Error ? e.message : String(e)}`,
        )
      } finally {
        setLoading(false)
      }
    },
    [setLoading, setLoadingStage, clearLoadingState, setLoadingProgress, setFileStatus, setModel, appendLog, addLoadingLog],
  )

  const handleRepairAll = React.useCallback(
    async (readyFiles: FileEntry[], options: RepairOptions) => {
      clearLog()
      for (const f of readyFiles) {
        await handleRepairOne(f.filepath, options, f.name)
      }
    },
    [clearLog, handleRepairOne],
  )

  return {
    handleBrowse,
    analyseFile,
    handleRepairOne,
    handleRepairAll,
  }
}
