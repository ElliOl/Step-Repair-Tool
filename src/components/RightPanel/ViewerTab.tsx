import * as React from 'react'
import * as SwitchPrimitive from '@radix-ui/react-switch'
import { useAppStore } from '../../stores/appStore'
import { useRepairActions } from '../../hooks/useRepairActions'

type Quality = 'fast' | 'standard' | 'fine'

const QUALITY_OPTIONS: { value: Quality; label: string; desc: string }[] = [
  { value: 'fast', label: 'Fast', desc: 'Low — quick preview' },
  { value: 'standard', label: 'Standard', desc: 'Balanced' },
  { value: 'fine', label: 'Fine', desc: 'High quality mesh' },
]

function SectionLabel({ children }: { children: React.ReactNode }) {
  return (
    <p className="text-xs font-semibold text-text-muted uppercase tracking-wider mb-2">
      {children}
    </p>
  )
}

function ToggleRow({
  label,
  checked,
  onCheckedChange,
}: {
  label: string
  checked: boolean
  onCheckedChange: (v: boolean) => void
}) {
  return (
    <label className="flex items-center justify-between gap-3 text-sm text-text cursor-pointer">
      <span>{label}</span>
      <SwitchPrimitive.Root
        checked={checked}
        onCheckedChange={onCheckedChange}
        className="relative h-5 w-9 cursor-pointer rounded-full outline-none transition-colors bg-surface-hover data-[state=checked]:bg-accent"
      >
        <SwitchPrimitive.Thumb className="block h-4 w-4 rounded-full bg-white shadow-sm transition-transform data-[state=checked]:translate-x-[18px] data-[state=unchecked]:translate-x-0.5" />
      </SwitchPrimitive.Root>
    </label>
  )
}

export function ViewerTab() {
  const showEdges = useAppStore((s) => s.showEdges)
  const showFaces = useAppStore((s) => s.showFaces)
  const meshQuality = useAppStore((s) => s.meshQuality)
  const upAxis = useAppStore((s) => s.upAxis)
  const currentFile = useAppStore((s) => {
    // Get the first "done" or "ready" file's filepath for re-loading
    const f = s.files.find((f) => f.status === 'done' || f.status === 'ready')
    return f?.filepath ?? null
  })
  const setShowEdges = useAppStore((s) => s.setShowEdges)
  const setShowFaces = useAppStore((s) => s.setShowFaces)
  const setMeshQuality = useAppStore((s) => s.setMeshQuality)
  const setUpAxis = useAppStore((s) => s.setUpAxis)
  const setModel = useAppStore((s) => s.setModel)
  const setLoading = useAppStore((s) => s.setLoading)
  const appendLog = useAppStore((s) => s.appendLog)

  const applyQuality = React.useCallback(
    async (q: Quality) => {
      setMeshQuality(q)
      if (!currentFile) return
      setLoading(true)
      try {
        const result = await window.electronAPI.loadStepMesh(currentFile, q)
        setModel(
          {
            shapeId: result.shapeId,
            mesh: result.mesh,
            edges: result.edges,
            parts: result.parts,
          },
          currentFile.split(/[/\\]/).pop() ?? null,
        )
        appendLog(`[${new Date().toLocaleTimeString()}] Quality → ${q}`)
      } catch (e) {
        appendLog(`[${new Date().toLocaleTimeString()}] Quality reload failed: ${e instanceof Error ? e.message : String(e)}`)
      } finally {
        setLoading(false)
      }
    },
    [currentFile, setMeshQuality, setModel, setLoading, appendLog],
  )

  return (
    <div className="flex flex-col gap-5 p-4">
      {/* Display */}
      <div>
        <SectionLabel>Display</SectionLabel>
        <div className="space-y-3">
          <ToggleRow label="Show faces" checked={showFaces} onCheckedChange={setShowFaces} />
          <ToggleRow label="Show edges" checked={showEdges} onCheckedChange={setShowEdges} />
        </div>
      </div>

      {/* Up axis */}
      <div>
        <SectionLabel>Vertical axis</SectionLabel>
        <div className="grid grid-cols-2 gap-1">
          {(['+Y', '+Z'] as const).map((axis) => (
            <button
              key={axis}
              type="button"
              onClick={() => setUpAxis(axis)}
              className={[
                'py-1.5 rounded-md border text-xs font-medium transition-colors',
                upAxis === axis
                  ? 'border-accent bg-accent-muted text-text'
                  : 'border-border bg-surface-elevated text-text-muted hover:bg-surface-hover hover:text-text',
              ].join(' ')}
            >
              {axis === '+Y' ? 'Y-up' : 'Z-up'}
            </button>
          ))}
        </div>
        <p className="mt-1.5 text-xs text-text-muted/60">Which axis is vertical in your file</p>
      </div>

      {/* Mesh quality */}
      <div>
        <SectionLabel>Mesh quality</SectionLabel>
        <div className="space-y-1.5">
          {QUALITY_OPTIONS.map((opt) => (
            <button
              key={opt.value}
              type="button"
              onClick={() => applyQuality(opt.value)}
              className={[
                'w-full flex items-center justify-between px-3 py-2 rounded-md border text-sm transition-colors',
                meshQuality === opt.value
                  ? 'border-accent bg-accent-muted text-text'
                  : 'border-border bg-surface-elevated text-text-muted hover:bg-surface-hover hover:text-text',
              ].join(' ')}
            >
              <span className="font-medium">{opt.label}</span>
              <span className="text-xs opacity-70">{opt.desc}</span>
            </button>
          ))}
        </div>
        {!currentFile && (
          <p className="mt-2 text-xs text-text-muted/60 italic">
            Load a file to change quality.
          </p>
        )}
      </div>
    </div>
  )
}
