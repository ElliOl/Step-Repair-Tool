/**
 * Step Viewer - STEP load, tessellation, mesh/edge extraction for CAD viewer
 * Adapted from hive step_loader. No HLR, single quality for display.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>

#include <TopoDS_Shape.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <Standard_TypeDef.hxx>

namespace StepFixerNative {

struct MeshData {
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<uint32_t> indices;
  float bbox_min[3];
  float bbox_max[3];
  int face_count;
  int triangle_count;
  std::vector<std::string> logs;
};

struct EdgeData {
  std::vector<float> positions;
  std::vector<uint32_t> indices;
  int edge_count;
};

struct PartNode {
  std::string id;
  std::string name;
  /** NAUO instance label name — the "real" name hidden in the broken STEP file.
   *  Non-empty only when name == "0" and a NEXT_ASSEMBLY_USAGE_OCCURRENCE instance
   *  label carried a different name. After repair this equals name. */
  std::string instanceName;
  std::string parentId;
  std::vector<std::string> childIds;
  bool hasColor;
  float color[3];
  std::string shapeType;
  bool isAssembly;
  uint32_t startVertex;
  uint32_t vertexCount;
  uint32_t startIndex;
  uint32_t indexCount;
  uint32_t startEdgePoint;
  uint32_t edgePointCount;
  uint32_t startEdgeIndex;
  uint32_t edgeIndexCount;
};

struct PartTreeData {
  std::vector<PartNode> parts;
  std::map<std::string, TopoDS_Shape> shapes;
};

/** Result of loading a STEP file for viewer */
struct LoadResult {
  std::string shape_id;
  MeshData mesh;
  EdgeData edges;
  PartTreeData part_tree;
};

class StepViewer {
public:
  StepViewer();
  ~StepViewer();

  using LogCallback = std::function<void(const std::string&)>;
  void SetLogCallback(LogCallback cb);

  /**
   * Load STEP file, tessellate, extract mesh + edges + part tree.
   * quality: "fast" | "standard" | "fine"
   */
  LoadResult LoadStepMesh(const std::string& filepath, const std::string& quality = "standard");

  /** Get raw shape by id (for repair pipeline) */
  TopoDS_Shape GetShape(const std::string& shape_id);

  /** Get XCAF document by shape id (for name_repair / shell_split) */
  Handle(TDocStd_Document) GetDocument(const std::string& shape_id);

  /**
   * Return the MANIFOLD_SOLID_BREP name map cached from the most recent
   * LoadStepMesh call for this filepath.  Maps TShape address → MSB entity
   * name so RepairNames can prefer Plasticity body names (e.g. "Solid 262.001")
   * over NAUO instance names, which may be a root filepath for embedded files.
   * Returns an empty map if the path has not been loaded or was invalidated.
   */
  std::unordered_map<Standard_Address, std::string>
      GetBrepNameMap(const std::string& filepath) const;

  /**
   * Return the raw file bytes cached from the most recent LoadStepMesh call
   * for this filepath.  Empty string if the path has not been loaded or the
   * cache was invalidated.  Used by the text-level repair path to avoid
   * re-reading the file from disk.
   */
  std::string GetRawContent(const std::string& filepath) const;

  /**
   * Return the XCAF document cached from the most recent LoadStepMesh call
   * for this filepath.  Null handle if not cached.  Used by the OCCT repair
   * path to avoid re-running ReadFile + Transfer.
   */
  Handle(TDocStd_Document) GetDocumentForPath(const std::string& filepath) const;

  /**
   * Drop the cached raw content and doc for this filepath.  Called after
   * a repair write so subsequent repairs re-read from disk (the on-disk
   * file may have been replaced by the fixed version).
   */
  void InvalidatePath(const std::string& filepath);

  /**
   * Rebuild the viewer result for a text-patched STEP file without any OCCT
   * file I/O or re-tessellation.
   *
   * The text repair path only changes metadata (names, styling entities) —
   * the geometry is identical to the original.  This method:
   *   1. Looks up the cached shape and doc by origFilepath.
   *   2. Re-extracts the part tree from the (already-mutated) doc.
   *   3. Re-reads the existing triangulation off the TShape objects — no
   *      BRepMesh_IncrementalMesh call, so this is milliseconds not minutes.
   *   4. Stores the result under a new shape_id keyed to outputFilepath.
   *
   * Throws if no cached shape exists for origFilepath.
   */
  LoadResult RebuildFromCachedShape(
      const std::string& origFilepath,
      const std::string& outputFilepath,
      const std::string& quality = "standard");

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace StepFixerNative
