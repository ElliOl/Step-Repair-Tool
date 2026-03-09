import * as React from 'react'
import * as THREE from 'three'
import type { EdgeData, PartNode } from '../../types'

const EDGE_COLOR = new THREE.Color(0.3, 0.3, 0.32)

interface CADEdgesProps {
  edges: EdgeData
  parts?: PartNode[]
  partVisibility?: Record<string, boolean>
}

export function CADEdges({ edges, parts, partVisibility }: CADEdgesProps) {
  const geometry = React.useMemo(() => {
    const geom = new THREE.BufferGeometry()
    geom.setAttribute('position', new THREE.BufferAttribute(edges.positions, 3))
    const lineIndices: number[] = []

    if (parts && partVisibility) {
      // startEdgeIndex and edgeIndexCount are in polyline units (each polyline = 2 slots
      // in the flat Uint32Array: [startPointIdx, pointCount]). Multiply by 2 to get the
      // flat array offset.
      const leafParts = parts.filter((p) => p.edgeIndexCount > 0 && !p.isAssembly)
      for (const p of leafParts) {
        if (partVisibility[p.id] === false) continue
        const flatStart = p.startEdgeIndex * 2
        const flatEnd = (p.startEdgeIndex + p.edgeIndexCount) * 2
        for (let i = flatStart; i < flatEnd; i += 2) {
          const startIdx = edges.indices[i]
          const pointCount = edges.indices[i + 1]
          for (let j = 0; j < pointCount - 1; j++) {
            lineIndices.push(startIdx + j)
            lineIndices.push(startIdx + j + 1)
          }
        }
      }
    } else {
      for (let i = 0; i < edges.indices.length; i += 2) {
        const startIdx = edges.indices[i]
        const pointCount = edges.indices[i + 1]
        for (let j = 0; j < pointCount - 1; j++) {
          lineIndices.push(startIdx + j)
          lineIndices.push(startIdx + j + 1)
        }
      }
    }

    geom.setIndex(lineIndices)
    return geom
  }, [edges, parts, partVisibility])

  return (
    <lineSegments geometry={geometry}>
      <lineBasicMaterial color={EDGE_COLOR} />
    </lineSegments>
  )
}
