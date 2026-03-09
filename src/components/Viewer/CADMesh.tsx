import * as React from 'react'
import * as THREE from 'three'
import type { MeshData, PartNode } from '../../types'

const DEFAULT_COLOR = new THREE.Color(0.72, 0.72, 0.74)
// Selection: bright blue-white tint
const SELECT_COLOR = new THREE.Color(0.45, 0.65, 1.0)
// Hover: warm white tint
const HOVER_COLOR = new THREE.Color(0.85, 0.85, 0.95)

interface CADMeshProps {
  mesh: MeshData
  parts: PartNode[]
  partVisibility?: Record<string, boolean>
  selectedPartIds?: Set<string>
  hoveredPartId?: string | null
}

export function CADMesh({
  mesh,
  parts,
  partVisibility,
  selectedPartIds,
  hoveredPartId,
}: CADMeshProps) {
  const geometry = React.useMemo(() => {
    const vertexCount = mesh.positions.length / 3
    const colorArray = new Float32Array(vertexCount * 3)

    // Fill with default color
    for (let i = 0; i < vertexCount; i++) {
      colorArray[i * 3] = DEFAULT_COLOR.r
      colorArray[i * 3 + 1] = DEFAULT_COLOR.g
      colorArray[i * 3 + 2] = DEFAULT_COLOR.b
    }

    // Paint per-part colors with interaction state applied, skipping hidden parts
    const leafParts = parts.filter((p) => p.vertexCount > 0 && !p.isAssembly)
    const hiddenIds = new Set<string>()

    for (const p of leafParts) {
      const isHidden = partVisibility ? partVisibility[p.id] === false : false
      if (isHidden) {
        hiddenIds.add(p.id)
        continue
      }

      const hasColor = Array.isArray(p.color) && p.color !== null
      const baseR = hasColor ? (p.color as [number, number, number])[0] : DEFAULT_COLOR.r
      const baseG = hasColor ? (p.color as [number, number, number])[1] : DEFAULT_COLOR.g
      const baseB = hasColor ? (p.color as [number, number, number])[2] : DEFAULT_COLOR.b

      const isSelected = selectedPartIds?.has(p.id) ?? false
      const isHovered = hoveredPartId === p.id

      let r: number, g: number, b: number

      if (isSelected) {
        // Blend original color 50% with selection tint
        r = baseR * 0.5 + SELECT_COLOR.r * 0.5
        g = baseG * 0.5 + SELECT_COLOR.g * 0.5
        b = baseB * 0.5 + SELECT_COLOR.b * 0.5
      } else if (isHovered) {
        // Blend original color 60% with hover tint
        r = baseR * 0.6 + HOVER_COLOR.r * 0.4
        g = baseG * 0.6 + HOVER_COLOR.g * 0.4
        b = baseB * 0.6 + HOVER_COLOR.b * 0.4
      } else {
        r = baseR
        g = baseG
        b = baseB
      }

      const end = p.startVertex + p.vertexCount
      for (let i = p.startVertex; i < end; i++) {
        colorArray[i * 3] = r
        colorArray[i * 3 + 1] = g
        colorArray[i * 3 + 2] = b
      }
    }

    // Build a filtered index buffer that excludes hidden parts' triangles entirely
    let visibleIndexCount = 0
    for (const p of leafParts) {
      if (!hiddenIds.has(p.id)) visibleIndexCount += p.indexCount
    }
    const filteredIndices = new Uint32Array(visibleIndexCount)
    let offset = 0
    for (const p of leafParts) {
      if (!hiddenIds.has(p.id)) {
        filteredIndices.set(mesh.indices.subarray(p.startIndex, p.startIndex + p.indexCount), offset)
        offset += p.indexCount
      }
    }

    const geom = new THREE.BufferGeometry()
    geom.setAttribute('position', new THREE.BufferAttribute(mesh.positions, 3))
    // Normals are unit vectors from the C++ analytical pass — mark normalized so
    // Three.js/WebGL don't re-normalize them in the vertex shader.
    geom.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3, true))
    geom.setAttribute('color', new THREE.BufferAttribute(colorArray, 3))
    geom.setIndex(new THREE.BufferAttribute(filteredIndices, 1))
    geom.computeBoundingBox()
    geom.computeBoundingSphere()
    return geom
  }, [mesh, parts, partVisibility, selectedPartIds, hoveredPartId])

  return (
    <mesh geometry={geometry}>
      <meshStandardMaterial
        vertexColors
        flatShading={false}
        side={THREE.DoubleSide}
        roughness={0.55}
        metalness={0.1}
      />
    </mesh>
  )
}
