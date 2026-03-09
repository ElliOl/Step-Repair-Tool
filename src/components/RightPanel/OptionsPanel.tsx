import * as React from 'react'
import * as SwitchPrimitive from '@radix-ui/react-switch'
import { useAppStore } from '../../stores/appStore'

function OptionRow({
  label,
  description,
  checked,
  onCheckedChange,
}: {
  label: string
  description?: string
  checked: boolean
  onCheckedChange: (v: boolean) => void
}) {
  return (
    <label className="flex items-start justify-between gap-3 cursor-pointer group">
      <div className="flex flex-col gap-0.5 pt-0.5">
        <span className="text-sm text-text leading-tight">{label}</span>
        {description && (
          <span className="text-[10px] text-text-muted leading-tight">{description}</span>
        )}
      </div>
      <SwitchPrimitive.Root
        checked={checked}
        onCheckedChange={onCheckedChange}
        className="relative shrink-0 h-5 w-9 cursor-pointer rounded-full outline-none transition-colors bg-surface-hover data-[state=checked]:bg-accent"
      >
        <SwitchPrimitive.Thumb className="block h-4 w-4 rounded-full bg-white shadow-sm transition-transform data-[state=checked]:translate-x-[18px] data-[state=unchecked]:translate-x-0.5" />
      </SwitchPrimitive.Root>
    </label>
  )
}

export function OptionsPanel() {
  const { options, setOptions } = useAppStore()
  return (
    <div className="space-y-3">
      <p className="text-xs font-semibold text-text-muted uppercase tracking-wider">Options</p>
      <div className="space-y-3">
        <OptionRow
          label="Fix part names"
          description="Copy NAUO instance names to unnamed prototypes"
          checked={options.fixNames}
          onCheckedChange={(v) => setOptions({ fixNames: v })}
        />
        <OptionRow
          label="Fix HOOPS Exchange compatibility"
          description="Strip per-face color overrides that can cause geometry import errors"
          checked={options.fixHoopsCompat}
          onCheckedChange={(v) => setOptions({ fixHoopsCompat: v })}
        />
        <OptionRow
          label="Split disconnected solids"
          description="When a solid contains multiple unconnected face groups, split into separate solids (slow)"
          checked={options.fixShells}
          onCheckedChange={(v) => setOptions({ fixShells: v })}
        />
      </div>
    </div>
  )
}
