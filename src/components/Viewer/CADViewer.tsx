import * as React from 'react'
import { Canvas, useThree } from '@react-three/fiber'
import { OrbitControls, Bounds, GizmoHelper, GizmoViewport } from '@react-three/drei'
import * as THREE from 'three'
import { CADMesh } from './CADMesh'
import { CADEdges } from './CADEdges'
import type { ViewerModel } from '../../types'

const VIEWPORT_BG = '#27272a'

// Compute bounding center + radius from raw positions buffer
function computeModelBounds(positions: Float32Array) {
  let minX = Infinity, minY = Infinity, minZ = Infinity
  let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity
  for (let i = 0; i < positions.length; i += 3) {
    const x = positions[i], y = positions[i + 1], z = positions[i + 2]
    if (x < minX) minX = x; if (x > maxX) maxX = x
    if (y < minY) minY = y; if (y > maxY) maxY = y
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z
  }
  const cx = (minX + maxX) / 2
  const cy = (minY + maxY) / 2
  const cz = (minZ + maxZ) / 2
  const dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ
  const radius = Math.sqrt(dx * dx + dy * dy + dz * dz) / 2
  return { center: new THREE.Vector3(cx, cy, cz), radius }
}

// ─── SceneRotation ────────────────────────────────────────────────────────────
// Positions the pivot at the geometry center, applies quaternion rotation with
// smooth slerp (matching Trace), and counters the offset inside so the mesh
// vertices stay at their original world positions when rotation = 0.
type UpAxis = '+Y' | '+Z'

interface SceneRotationProps {
  center: THREE.Vector3
  upAxis: UpAxis
  viewRotationX: number
  viewRotationZ: number
  children: React.ReactNode
}

function SceneRotation({ center, upAxis, viewRotationX, viewRotationZ, children }: SceneRotationProps) {
  const groupRef = React.useRef<THREE.Group>(null)
  const { invalidate } = useThree()

  React.useEffect(() => {
    if (!groupRef.current) return

    // 1) Display correction: rotate the selected up axis to point at screen-up (world Y).
    //    Y-up → identity (no change). Z-up → tilts model so Z faces up on screen.
    const modelUp = upAxis === '+Y' ? new THREE.Vector3(0, 1, 0) : new THREE.Vector3(0, 0, 1)
    const qUp = new THREE.Quaternion().setFromUnitVectors(modelUp, new THREE.Vector3(0, 1, 0))

    // 2) Arrow keys: always screen-space global, independent of up axis.
    //    Turntable: always around world Y (screen vertical — the selected up axis
    //    already points there thanks to qUp, so it naturally spins around "up").
    //    Flip: always around world X (screen horizontal).
    const qTurntable = new THREE.Quaternion().setFromAxisAngle(
      new THREE.Vector3(0, 1, 0),
      THREE.MathUtils.degToRad(viewRotationZ),
    )
    const qFlip = new THREE.Quaternion().setFromAxisAngle(
      new THREE.Vector3(1, 0, 0),
      THREE.MathUtils.degToRad(viewRotationX),
    )

    // Display correction first, then flip, then turntable outermost (world-space)
    const target = new THREE.Quaternion()
      .multiplyQuaternions(qTurntable, new THREE.Quaternion().multiplyQuaternions(qFlip, qUp))

    let frame: number
    const animate = () => {
      if (!groupRef.current) return
      groupRef.current.quaternion.slerp(target, 0.15)
      if (groupRef.current.quaternion.angleTo(target) > 0.001) {
        frame = requestAnimationFrame(animate)
        invalidate()
      } else {
        groupRef.current.quaternion.copy(target)
        invalidate()
      }
    }
    frame = requestAnimationFrame(animate)
    invalidate()
    return () => cancelAnimationFrame(frame)
  }, [upAxis, viewRotationX, viewRotationZ, invalidate])

  return (
    <group ref={groupRef} position={center}>
      {/* Counter-offset so vertices stay at original world positions when rotation=0 */}
      <group position={[-center.x, -center.y, -center.z]}>
        {children}
      </group>
    </group>
  )
}

// ─── GizmoWithUpAxis ──────────────────────────────────────────────────────────
// Rotates the gizmo axes only when the up-axis setting changes (Y or Z up).
// Arrow key rotations are intentionally NOT reflected — the gizmo is a fixed
// reference showing which axis is "up" in the current coordinate convention.
function GizmoWithUpAxis({ upAxis }: { upAxis: UpAxis }) {
  const groupRef = React.useRef<THREE.Group>(null)
  const { invalidate } = useThree()

  React.useEffect(() => {
    if (!groupRef.current) return
    const modelUp = upAxis === '+Y' ? new THREE.Vector3(0, 1, 0) : new THREE.Vector3(0, 0, 1)
    const target = new THREE.Quaternion().setFromUnitVectors(modelUp, new THREE.Vector3(0, 1, 0))

    let frame: number
    const animate = () => {
      if (!groupRef.current) return
      groupRef.current.quaternion.slerp(target, 0.15)
      if (groupRef.current.quaternion.angleTo(target) > 0.001) {
        frame = requestAnimationFrame(animate)
        invalidate()
      } else {
        groupRef.current.quaternion.copy(target)
        invalidate()
      }
    }
    frame = requestAnimationFrame(animate)
    invalidate()
    return () => cancelAnimationFrame(frame)
  }, [upAxis, invalidate])

  return (
    <group ref={groupRef}>
      <GizmoViewport axisColors={['#f2555a', '#8b5cf6', '#ffb224']} labelColor="white" />
    </group>
  )
}

// ─── CADViewer ────────────────────────────────────────────────────────────────
interface CADViewerProps {
  model: ViewerModel | null
  showEdges?: boolean
  showFaces?: boolean
  partVisibility?: Record<string, boolean>
  selectedPartIds?: Set<string>
  hoveredPartId?: string | null
  upAxis?: UpAxis
  viewRotationX?: number
  viewRotationZ?: number
}

export function CADViewer({
  model,
  showEdges = true,
  showFaces = true,
  partVisibility,
  selectedPartIds,
  hoveredPartId,
  upAxis = '+Z',
  viewRotationX = 0,
  viewRotationZ = 0,
}: CADViewerProps) {
  // Recompute only when the underlying geometry changes (new file load)
  const modelBounds = React.useMemo(
    () => (model ? computeModelBounds(model.mesh.positions) : null),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [model?.shapeId],
  )

  return (
    <div className="w-full h-full" style={{ background: VIEWPORT_BG }}>
      <Canvas
        gl={{ antialias: true, alpha: false, powerPreference: 'high-performance' }}
        camera={{ position: [0, 0, 5], fov: 45, near: 0.001, far: 1_000_000 }}
        style={{ width: '100%', height: '100%', background: VIEWPORT_BG }}
      >
        <color attach="background" args={[VIEWPORT_BG]} />

        <ambientLight intensity={0.65} />
        <directionalLight position={[1, 2, 1.5]} intensity={0.9} />
        <directionalLight position={[-1, -1, -0.8]} intensity={0.3} />
        <directionalLight position={[0, -1, 0]} intensity={0.15} />

        {model && modelBounds && (
          // Bounds fits camera once per model load (key resets on new file).
          // observe is off so it never re-fits during rotation.
          <Bounds key={model.shapeId} fit clip margin={1.2}>
            <SceneRotation
              center={modelBounds.center}
              upAxis={upAxis}
              viewRotationX={viewRotationX}
              viewRotationZ={viewRotationZ}
            >
              {showFaces && (
                <CADMesh
                  mesh={model.mesh}
                  parts={model.parts}
                  partVisibility={partVisibility}
                  selectedPartIds={selectedPartIds}
                  hoveredPartId={hoveredPartId}
                />
              )}
              {showEdges && model.edges.positions.length > 0 && (
                <CADEdges edges={model.edges} parts={model.parts} partVisibility={partVisibility} />
              )}
            </SceneRotation>
          </Bounds>
        )}

        <OrbitControls makeDefault enableDamping dampingFactor={0.07} />

        <GizmoHelper alignment="bottom-right" margin={[80, 80]}>
          <GizmoWithUpAxis upAxis={upAxis} />
        </GizmoHelper>
      </Canvas>
    </div>
  )
}
