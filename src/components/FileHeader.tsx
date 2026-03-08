import * as React from 'react'
import { Minus, Square, X } from 'lucide-react'

interface FileHeaderProps {
  fileName: string | null
}

const isMac = typeof window !== 'undefined' && window.electronAPI?.platform === 'darwin'

function WinControls() {
  const [maximized, setMaximized] = React.useState(false)

  const handleMinimize = () => window.electronAPI?.windowMinimize()
  const handleMaximize = () => {
    window.electronAPI?.windowMaximize()
    setMaximized((v) => !v)
  }
  const handleClose = () => window.electronAPI?.windowClose()

  return (
    <div
      className="flex items-stretch shrink-0 h-full"
      style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties}
    >
      <button
        type="button"
        onClick={handleMinimize}
        className="flex items-center justify-center w-11 h-full text-text-muted hover:bg-surface-elevated hover:text-text transition-colors"
        aria-label="Minimize"
      >
        <Minus className="w-3.5 h-3.5" />
      </button>
      <button
        type="button"
        onClick={handleMaximize}
        className="flex items-center justify-center w-11 h-full text-text-muted hover:bg-surface-elevated hover:text-text transition-colors"
        aria-label={maximized ? 'Restore' : 'Maximize'}
      >
        <Square className="w-3 h-3" />
      </button>
      <button
        type="button"
        onClick={handleClose}
        className="flex items-center justify-center w-11 h-full text-text-muted hover:bg-red-600 hover:text-white transition-colors"
        aria-label="Close"
      >
        <X className="w-3.5 h-3.5" />
      </button>
    </div>
  )
}

export function FileHeader({ fileName }: FileHeaderProps) {
  return (
    <header
      className="flex items-center shrink-0 bg-background select-none"
      style={{
        height: '32px',
        WebkitAppRegion: 'drag',
      } as React.CSSProperties}
    >
      {/* Left — app name. On macOS, pad right of the traffic lights (~76px). */}
      <div
        className="flex items-center gap-2 shrink-0 px-3"
        style={{ paddingLeft: isMac ? '80px' : '12px' }}
      >
        <span className="text-xs font-semibold text-text tracking-wide">StairRepair</span>
      </div>

      {/* Center — filename */}
      <div className="flex-1 flex items-center justify-center min-w-0 px-2">
        {fileName && (
          <span className="text-xs text-text-muted truncate max-w-xs" title={fileName}>
            {fileName}
          </span>
        )}
      </div>

      {/* Right — window controls (Windows / Linux only) */}
      {!isMac && <WinControls />}
    </header>
  )
}
