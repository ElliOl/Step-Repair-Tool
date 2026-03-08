/**
 * Step Viewer Implementation
 */

#include "step_viewer.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

#include <STEPCAFControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <StepShape_ManifoldSolidBrep.hxx>
#include <StepVisual_OverRidingStyledItem.hxx>
#include <StepRepr_RepresentationItem.hxx>
#include <XSControl_WorkSession.hxx>
#include <XSControl_TransferReader.hxx>
#include <Transfer_TransientProcess.hxx>
#include <TransferBRep_ShapeBinder.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_ChildIterator.hxx>
#include <Quantity_Color.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Iterator.hxx>
#include <BRep_Builder.hxx>

#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_XYZ.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <Geom_Surface.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <Standard_Version.hxx>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace StepFixerNative {

// ---------------------------------------------------------------------------
// Progress indicator — streams percentage updates during reader.Transfer().
// Show() is called by OCCT's internal progress machinery whenever it crosses
// a sub-scope boundary. We throttle to ~2% increments to avoid flooding the
// renderer with IPC messages.
// ---------------------------------------------------------------------------
class TransferProgressIndicator : public Message_ProgressIndicator {
  DEFINE_STANDARD_RTTIEXT(TransferProgressIndicator, Message_ProgressIndicator)
public:
  using LogFn = std::function<void(const std::string&)>;

  explicit TransferProgressIndicator(LogFn fn)
    : myLog(std::move(fn)), myLastPct(-1) {}

  Standard_Boolean UserBreak() override { return Standard_False; }

  void Show(const Message_ProgressScope& /*theScope*/,
            const Standard_Boolean isForced) override {
    int pct = static_cast<int>(GetPosition() * 100.0);
    if (pct > myLastPct + 1 || (isForced && pct > myLastPct)) {
      myLastPct = pct;
      std::ostringstream oss;
      oss << "Parsing geometry... " << pct << "%";
      myLog(oss.str());
    }
  }

private:
  LogFn myLog;
  int myLastPct;
};
IMPLEMENT_STANDARD_RTTIEXT(TransferProgressIndicator, Message_ProgressIndicator)

#define LOG_AND_STORE(impl, message) \
  do { \
    std::ostringstream __log; __log << message; \
    std::string __msg = __log.str(); \
    impl->logs.push_back(__msg); \
    if (impl->log_callback) impl->log_callback(__msg); \
  } while(0)

static std::string ShapeTypeToString(TopAbs_ShapeEnum t) {
  switch (t) {
    case TopAbs_COMPOUND: return "COMPOUND";
    case TopAbs_COMPSOLID: return "COMPSOLID";
    case TopAbs_SOLID: return "SOLID";
    case TopAbs_SHELL: return "SHELL";
    case TopAbs_FACE: return "FACE";
    case TopAbs_WIRE: return "WIRE";
    case TopAbs_EDGE: return "EDGE";
    case TopAbs_VERTEX: return "VERTEX";
    default: return "SHAPE";
  }
}

static int EdgeSamples(const BRepAdaptor_Curve& curve_adaptor, const std::string& quality, double first, double last) {
  GeomAbs_CurveType ct = curve_adaptor.GetType();
  double len = 0.0;
  try { len = GCPnts_AbscissaPoint::Length(curve_adaptor, first, last); } catch (...) { len = std::abs(last - first); }
  int base = (quality == "fine") ? 100 : (quality == "standard") ? 50 : 24;
  if (ct == GeomAbs_Line) return 1;
  if (ct == GeomAbs_Circle) {
    gp_Circ c = curve_adaptor.Circle();
    double circ = 2.0 * M_PI * c.Radius();
    double frac = len / circ;
    int s = (int)(base * frac);
    return std::max(8, std::min(s, base));
  }
  if (ct == GeomAbs_BSplineCurve || ct == GeomAbs_BezierCurve) {
    double seg = (quality == "fine") ? 0.3 : (quality == "standard") ? 0.6 : 1.0;
    int s = (int)(len / seg);
    return std::max(10, std::min(s, base * 3));
  }
  double seg = (quality == "fine") ? 0.5 : 1.0;
  int s = (int)(len / seg);
  return std::max(8, std::min(s, base * 2));
}

struct StepViewer::Impl {
  std::unordered_map<std::string, TopoDS_Shape>              shapes;
  std::unordered_map<std::string, Handle(TDocStd_Document)>  docs;
  std::unordered_map<std::string, PartTreeData>              part_trees;
  std::unordered_map<std::string, LoadResult>                load_cache;
  // filepath-keyed caches — allow repair to reuse what analyse already loaded
  std::unordered_map<std::string, std::string>               raw_by_path;      // filepath → raw bytes
  std::unordered_map<std::string, std::string>               shape_id_by_path; // filepath → last shape_id
  // shape_id-keyed MSB name map: TShape address → MANIFOLD_SOLID_BREP name.
  // Built once during LoadStepMesh and reused by RepairNames in the text path
  // so that Plasticity body names are preferred over NAUO instance names even
  // when the WorkSession is no longer available.
  std::unordered_map<std::string,
      std::unordered_map<Standard_Address, std::string>>     brep_name_maps;
  int next_id = 1;
  std::vector<std::string> logs;
  LogCallback log_callback;
};

StepViewer::StepViewer() : pImpl(std::make_unique<Impl>()) {}
StepViewer::~StepViewer() = default;

void StepViewer::SetLogCallback(LogCallback cb) { pImpl->log_callback = cb; }

static void ProcessLabelRecursive(
  const TDF_Label& label,
  const std::string& parentId,
  const Handle(XCAFDoc_ShapeTool)& shapeTool,
  const Handle(XCAFDoc_ColorTool)& colorTool,
  PartTreeData& data,
  int& partIndex,
  int& groupCounter,
  int& partCounter,
  const TopLoc_Location& accumulatedLocation = TopLoc_Location(),
  const std::string& nauoInstanceName = ""
) {
  TopoDS_Shape shape = shapeTool->GetShape(label);
  if (shape.IsNull()) return;

  PartNode part;
  part.id = "part_" + std::to_string(partIndex++);
  part.parentId = parentId;
  part.hasColor = false;
  part.startVertex = part.vertexCount = part.startIndex = part.indexCount = 0;
  part.startEdgePoint = part.edgePointCount = part.startEdgeIndex = part.edgeIndexCount = 0;

  Handle(TDataStd_Name) nameAttr;
  if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
    TCollection_AsciiString asciiName(nameAttr->Get());
    std::string extractedName = asciiName.ToCString();
    // Keep the raw PRODUCT name as-is (including '0') so the tree mirrors exactly
    // what Creo / KeyShot would display for the unrepaired file.
    if (!extractedName.empty() && extractedName != "COMPOUND" && extractedName != "COMPSOLID")
      part.name = extractedName;
  }
  // Store the NAUO instance name that pointed to this prototype label.
  // When the PRODUCT name is '0', this reveals the "real" name hidden in the
  // NEXT_ASSEMBLY_USAGE_OCCURRENCE lines — shown as a hint in the part tree.
  part.instanceName = nauoInstanceName;
  if (part.name.empty()) {
    bool willBeFolder = shapeTool->IsAssembly(label) || shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID;
    part.name = willBeFolder ? ("Group " + std::to_string(groupCounter++)) : ("Part " + std::to_string(partCounter++));
  }

  part.shapeType = ShapeTypeToString(shape.ShapeType());
  part.isAssembly = shapeTool->IsAssembly(label);

  Quantity_Color color;
  if (colorTool->GetColor(label, XCAFDoc_ColorGen, color) ||
      colorTool->GetColor(shape, XCAFDoc_ColorGen, color) ||
      colorTool->GetColor(shape, XCAFDoc_ColorSurf, color) ||
      colorTool->GetColor(shape, XCAFDoc_ColorCurv, color)) {
    part.hasColor = true;
    part.color[0] = (float)color.Red();
    part.color[1] = (float)color.Green();
    part.color[2] = (float)color.Blue();
  }

  TopoDS_Shape transformedShape = shape;
  if (!accumulatedLocation.IsIdentity())
    transformedShape = shape.Located(accumulatedLocation);
  data.shapes[part.id] = transformedShape;

  std::string currentPartId = part.id;
  size_t currentPartIndex = data.parts.size();
  data.parts.push_back(part);

  if (part.isAssembly) {
    TDF_LabelSequence components;
    shapeTool->GetComponents(label, components);
    for (int j = 1; j <= components.Length(); j++) {
      TDF_Label componentLabel = components.Value(j);
      // Read the NAUO instance label name — this is the "real" part name that
      // Plasticity writes into NEXT_ASSEMBLY_USAGE_OCCURRENCE.  In unrepaired
      // files the PRODUCT (prototype) name is '0', but the NAUO label carries
      // the correct name.  We pass it down so the viewer can surface it as a
      // hint before the user runs the repair.
      std::string compInstanceName;
      Handle(TDataStd_Name) compNameAttr;
      if (componentLabel.FindAttribute(TDataStd_Name::GetID(), compNameAttr)) {
        TCollection_AsciiString cn(compNameAttr->Get());
        compInstanceName = cn.ToCString();
      }
      TopLoc_Location compLoc = shapeTool->GetLocation(componentLabel);
      TopLoc_Location composed = accumulatedLocation * compLoc;
      TDF_Label refLabel;
      if (shapeTool->GetReferredShape(componentLabel, refLabel)) {
        int startIndex = partIndex;
        ProcessLabelRecursive(refLabel, currentPartId, shapeTool, colorTool, data, partIndex, groupCounter, partCounter, composed, compInstanceName);
        if (partIndex > startIndex)
          data.parts[currentPartIndex].childIds.push_back("part_" + std::to_string(startIndex));
      } else {
        TopoDS_Shape childShape = shapeTool->GetShape(componentLabel);
        if (!childShape.IsNull()) {
          int startIndex = partIndex;
          ProcessLabelRecursive(componentLabel, currentPartId, shapeTool, colorTool, data, partIndex, groupCounter, partCounter, composed, compInstanceName);
          if (partIndex > startIndex)
            data.parts[currentPartIndex].childIds.push_back("part_" + std::to_string(startIndex));
        }
      }
    }
  } else if (shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID) {
    TopoDS_Iterator it(shape);
    while (it.More()) {
      TopoDS_Shape subShape = it.Value();
      TDF_Label subLabel;
      if (shapeTool->Search(subShape, subLabel)) {
        TopLoc_Location subLoc = subShape.Location();
        TopLoc_Location composed = accumulatedLocation * subLoc;
        int startIndex = partIndex;
        ProcessLabelRecursive(subLabel, currentPartId, shapeTool, colorTool, data, partIndex, groupCounter, partCounter, composed);
        if (partIndex > startIndex)
          data.parts[currentPartIndex].childIds.push_back("part_" + std::to_string(startIndex));
      } else {
        PartNode leafPart;
        leafPart.id = "part_" + std::to_string(partIndex++);
        leafPart.parentId = currentPartId;
        leafPart.name = "Part " + std::to_string(partCounter++);
        leafPart.shapeType = ShapeTypeToString(subShape.ShapeType());
        leafPart.isAssembly = false;
        leafPart.hasColor = false;
        leafPart.startVertex = leafPart.vertexCount = leafPart.startIndex = leafPart.indexCount = 0;
        leafPart.startEdgePoint = leafPart.edgePointCount = leafPart.startEdgeIndex = leafPart.edgeIndexCount = 0;
        TopoDS_Shape transformedSub = accumulatedLocation.IsIdentity() ? subShape : subShape.Located(accumulatedLocation);
        data.shapes[leafPart.id] = transformedSub;
        data.parts.push_back(leafPart);
        data.parts[currentPartIndex].childIds.push_back(leafPart.id);
      }
      it.Next();
    }
  }
}

static PartTreeData ExtractPartTree(
  const Handle(TDocStd_Document)& doc,
  const Handle(XCAFDoc_ShapeTool)& shapeTool,
  const Handle(XCAFDoc_ColorTool)& colorTool
) {
  PartTreeData data;
  TDF_LabelSequence freeShapes;
  shapeTool->GetFreeShapes(freeShapes);
  int partIndex = 0, groupCounter = 1, partCounter = 1;
  for (int i = 1; i <= freeShapes.Length(); i++)
    ProcessLabelRecursive(freeShapes.Value(i), "", shapeTool, colorTool, data, partIndex, groupCounter, partCounter);
  return data;
}

static double GetLinearDeflection(const std::string& quality) {
  if (quality == "fine") return 0.01;
  if (quality == "standard") return 0.1;
  return 1.0;
}
static double GetAngularDeflection(const std::string& quality) {
  if (quality == "fine") return 0.1;
  if (quality == "standard") return 0.2;
  return 0.5;
}

static void TessellateShape(const TopoDS_Shape& shape, double linear, double angular) {
  BRepMesh_IncrementalMesh mesher(shape, linear, Standard_False, angular, Standard_True);
  if (!mesher.IsDone()) throw std::runtime_error("Tessellation failed");
}

// Compute smooth normals from triangle cross-products for a single face's vertices.
// Used as fallback when UV/analytical normals are unavailable.
static void ComputeFaceCrossNormals(
  const Handle(Poly_Triangulation)& tri,
  bool rev,
  uint32_t baseVertex,          // index into mesh.positions/normals where this face starts
  Standard_Integer nbNodes,
  MeshData& mesh)
{
  std::vector<float> acc(nbNodes * 3, 0.0f);
  Standard_Integer nbTri = tri->NbTriangles();
  for (Standard_Integer t = 1; t <= nbTri; t++) {
    Standard_Integer i0, i1, i2;
    tri->Triangle(t).Get(i0, i1, i2);
    i0--; i1--; i2--;  // 0-indexed
    const float* p0 = &mesh.positions[(baseVertex + i0) * 3];
    const float* p1 = &mesh.positions[(baseVertex + i1) * 3];
    const float* p2 = &mesh.positions[(baseVertex + i2) * 3];
    float ex1 = p1[0]-p0[0], ey1 = p1[1]-p0[1], ez1 = p1[2]-p0[2];
    float ex2 = p2[0]-p0[0], ey2 = p2[1]-p0[1], ez2 = p2[2]-p0[2];
    float nx = ey1*ez2 - ez1*ey2;
    float ny = ez1*ex2 - ex1*ez2;
    float nz = ex1*ey2 - ey1*ex2;
    if (rev) { nx=-nx; ny=-ny; nz=-nz; }
    acc[i0*3]+=nx; acc[i0*3+1]+=ny; acc[i0*3+2]+=nz;
    acc[i1*3]+=nx; acc[i1*3+1]+=ny; acc[i1*3+2]+=nz;
    acc[i2*3]+=nx; acc[i2*3+1]+=ny; acc[i2*3+2]+=nz;
  }
  for (Standard_Integer i = 0; i < nbNodes; i++) {
    float nx = acc[i*3], ny = acc[i*3+1], nz = acc[i*3+2];
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-12f) { nx/=len; ny/=len; nz/=len; }
    else              { nx=0.0f; ny=0.0f; nz=1.0f; }
    mesh.normals[(baseVertex + i)*3]     = nx;
    mesh.normals[(baseVertex + i)*3 + 1] = ny;
    mesh.normals[(baseVertex + i)*3 + 2] = nz;
  }
}

static void ExtractMesh(const TopoDS_Shape& shape, MeshData& mesh) {
  mesh.positions.clear();
  mesh.normals.clear();
  mesh.indices.clear();
  int face_count = 0, triangle_count = 0;
  uint32_t vertex_offset = 0;

  for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
    const TopoDS_Face& face = TopoDS::Face(exp.Current());
    face_count++;

    // Triangulation + world transform
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull()) continue;
    gp_Trsf tr = loc.Transformation();
    bool rev = (face.Orientation() == TopAbs_REVERSED);
    Standard_Integer nbNodes = tri->NbNodes();

    // --- Positions ---
    for (Standard_Integer i = 1; i <= nbNodes; i++) {
      gp_Pnt pt = tri->Node(i).Transformed(tr);
      mesh.positions.push_back((float)pt.X());
      mesh.positions.push_back((float)pt.Y());
      mesh.positions.push_back((float)pt.Z());
    }

    // Pre-allocate normal slots for this face (filled below)
    mesh.normals.resize(mesh.positions.size(), 0.0f);

    // --- Analytical normals from parametric surface ---
    // BRep_Tool::Surface(face, surfLoc) returns the surface in its own coordinate
    // frame along with the location that places it in world space. Evaluating the
    // normal in surface space and then transforming avoids the silent identity-
    // location bug from the old BRep_Tool::Surface(face) call (no location arg).
    bool analyticalOk = false;
    if (tri->HasUVNodes()) {
      TopLoc_Location surfLoc;
      Handle(Geom_Surface) surface = BRep_Tool::Surface(face, surfLoc);
      if (!surface.IsNull()) {
        gp_Trsf surfTrsf = surfLoc.IsIdentity() ? gp_Trsf() : surfLoc.Transformation();
        analyticalOk = true;
        for (Standard_Integer i = 1; i <= nbNodes; i++) {
          gp_Pnt2d uv = tri->UVNode(i);
          GeomLProp_SLProps props(surface, uv.X(), uv.Y(), 1, 1e-6);
          if (props.IsNormalDefined()) {
            gp_Dir n = props.Normal();
            // Transform from surface-local to world space
            if (!surfLoc.IsIdentity()) n.Transform(surfTrsf);
            if (rev) n.Reverse();
            uint32_t idx = (vertex_offset + i - 1) * 3;
            mesh.normals[idx]     = (float)n.X();
            mesh.normals[idx + 1] = (float)n.Y();
            mesh.normals[idx + 2] = (float)n.Z();
          } else {
            // Degenerate point (pole etc.) — mark for cross-product fallback
            analyticalOk = false;
            break;
          }
        }
      }
    }

    // --- Fallback: per-face cross-product normals ---
    // Used when UV nodes are absent, surface is null, or a degenerate parametric
    // point was encountered. These are face-local (no cross-face averaging) so
    // hard edges at B-rep face boundaries are preserved correctly.
    if (!analyticalOk) {
      ComputeFaceCrossNormals(tri, rev, vertex_offset, nbNodes, mesh);
    }

    // --- Indices ---
    Standard_Integer nbTri = tri->NbTriangles();
    for (Standard_Integer i = 1; i <= nbTri; i++) {
      Standard_Integer n1, n2, n3;
      tri->Triangle(i).Get(n1, n2, n3);
      n1 = n1 - 1 + vertex_offset;
      n2 = n2 - 1 + vertex_offset;
      n3 = n3 - 1 + vertex_offset;
      if (rev) { mesh.indices.push_back(n1); mesh.indices.push_back(n3); mesh.indices.push_back(n2); }
      else     { mesh.indices.push_back(n1); mesh.indices.push_back(n2); mesh.indices.push_back(n3); }
      triangle_count++;
    }

    vertex_offset += nbNodes;
  }

  mesh.face_count = face_count;
  mesh.triangle_count = triangle_count;
}

static void ExtractEdges(const TopoDS_Shape& shape, EdgeData& edges, const std::string& quality) {
  std::vector<std::vector<float>> curves;
  for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
    const TopoDS_Edge& edge = TopoDS::Edge(exp.Current());
    BRepAdaptor_Curve curve_adaptor(edge);
    double first = curve_adaptor.FirstParameter(), last = curve_adaptor.LastParameter();
    int num_samples = EdgeSamples(curve_adaptor, quality, first, last);
    std::vector<float> pts;
    for (int i = 0; i <= num_samples; i++) {
      double t = first + (last - first) * i / num_samples;
      gp_Pnt pt = curve_adaptor.Value(t);
      pts.push_back((float)pt.X());
      pts.push_back((float)pt.Y());
      pts.push_back((float)pt.Z());
    }
    if (pts.size() >= 6) curves.push_back(pts);
  }
  uint32_t offset = 0;
  for (const auto& curve : curves) {
    edges.positions.insert(edges.positions.end(), curve.begin(), curve.end());
    uint32_t count = (uint32_t)(curve.size() / 3);
    edges.indices.push_back(offset);
    edges.indices.push_back(count);
    offset += count;
  }
  edges.edge_count = (int)curves.size();
}

/**
 * For HOOPS Exchange files, the MDGPR only covers a subset of faces.
 * We detect this by checking which faces have per-face color overrides in XCAF.
 * The two groups get separate PartNodes so the viewer shows the split that
 * Creo/Keyshot would see, giving the user a visual reason to run the fix.
 *
 * Returns true if the shape was split into covered + uncovered groups.
 */
static bool SplitSolidByFaceColor(
    const TopoDS_Shape& shape,
    const Handle(XCAFDoc_ColorTool)& colorTool,
    const std::unordered_set<Standard_Address>& styledFaceAddrs,
    TopoDS_Shape& covered,
    Quantity_Color& coveredColor,
    TopoDS_Shape& uncovered
) {
  BRep_Builder builder;
  TopoDS_Compound covComp, uncComp;
  builder.MakeCompound(covComp);
  builder.MakeCompound(uncComp);

  int covCount = 0, uncCount = 0;
  bool covColorSet = false;
  Quantity_Color fc;

  for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
    const TopoDS_Face& face = TopoDS::Face(fe.Current());
    bool hasFaceColor = false;

    // Primary: XCAF colorTool (works for standalone / free shapes).
    if (colorTool->GetColor(face, XCAFDoc_ColorSurf, fc) ||
        colorTool->GetColor(face, XCAFDoc_ColorGen, fc)) {
      hasFaceColor = true;
    }
    // Fallback: STEP entity transfer map.
    // When a solid is nested inside an assembly (accessed via GetReferredShape),
    // XCAF may not resolve per-face colors through the component reference.
    // The styledFaceAddrs set is built from OVER_RIDING_STYLED_ITEM entities
    // directly via the STEP reader transfer map, so it is reliable for both
    // standalone and assembly-embedded shapes.
    else if (!styledFaceAddrs.empty() &&
             styledFaceAddrs.count((Standard_Address)face.TShape().get()) > 0) {
      hasFaceColor = true;
      // Color not available from XCAF; use the typical Plasticity/HOOPS dark gray.
      fc = Quantity_Color(0.254902, 0.254902, 0.254902, Quantity_TOC_RGB);
    }

    if (hasFaceColor) {
      if (!covColorSet) { coveredColor = fc; covColorSet = true; }
      builder.Add(covComp, face);
      covCount++;
    } else {
      builder.Add(uncComp, face);
      uncCount++;
    }
  }

  if (covCount == 0 || uncCount == 0) return false;

  covered = covComp;
  uncovered = uncComp;
  return true;
}

static void ComputeBoundingBox(const TopoDS_Shape& shape, float bbox_min[3], float bbox_max[3]) {
  Bnd_Box bnd;
  BRepBndLib::Add(shape, bnd);
  double xmin, ymin, zmin, xmax, ymax, zmax;
  bnd.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  bbox_min[0] = (float)xmin; bbox_min[1] = (float)ymin; bbox_min[2] = (float)zmin;
  bbox_max[0] = (float)xmax; bbox_max[1] = (float)ymax; bbox_max[2] = (float)zmax;
}

LoadResult StepViewer::LoadStepMesh(const std::string& filepath, const std::string& quality) {
  pImpl->logs.clear();
  auto start = std::chrono::high_resolution_clock::now();
  LOG_AND_STORE(pImpl, "[StepViewer] Loading " << filepath << " (quality: " << quality << ")");

  // Cache the raw file bytes now so the repair worker can reuse them without
  // a second disk read.  We read the file ourselves first; OCCT's ReadFile will
  // also read it, but the OS page cache means the second read is essentially free.
  {
    std::ifstream rawf(filepath, std::ios::binary);
    if (rawf) {
      pImpl->raw_by_path[filepath] = std::string(
        (std::istreambuf_iterator<char>(rawf)),
        std::istreambuf_iterator<char>());
    }
  }

  Handle(TDocStd_Document) doc = new TDocStd_Document("MDTV-XCAF");
  STEPCAFControl_Reader reader;
  reader.SetColorMode(Standard_True);
  reader.SetNameMode(Standard_True);
  reader.SetLayerMode(Standard_True);
  auto t0 = std::chrono::high_resolution_clock::now();
  LOG_AND_STORE(pImpl, "Reading STEP file...");
  if (reader.ReadFile(filepath.c_str()) != IFSelect_RetDone)
    throw std::runtime_error("Failed to read STEP file: " + filepath);
  {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - t0).count();
    LOG_AND_STORE(pImpl, "File read in " << ms << "ms — building B-rep topology...");
  }

  // Stream incremental progress back to the renderer while OCCT resolves the
  // full STEP entity graph (this is the longest single step for large files).
  {
    auto logFn = pImpl->log_callback;
    Handle(TransferProgressIndicator) pi;
    if (logFn) {
      pi = new TransferProgressIndicator([logFn](const std::string& msg) { logFn(msg); });
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    if (!reader.Transfer(doc, logFn ? pi->Start() : Message_ProgressRange()))
      throw std::runtime_error("Failed to transfer to XCAF");
    auto ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::high_resolution_clock::now() - t1).count();
    LOG_AND_STORE(pImpl, "Topology built in " << ms1 << "ms");
  }

  Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());
  TDF_LabelSequence freeShapes;
  shapeTool->GetFreeShapes(freeShapes);
  if (freeShapes.Length() == 0)
    throw std::runtime_error("No shapes in STEP file");

  TopoDS_Shape shape;
  if (freeShapes.Length() == 1) {
    shape = shapeTool->GetShape(freeShapes.Value(1));
  } else {
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (int i = 1; i <= freeShapes.Length(); i++) {
      TopoDS_Shape s = shapeTool->GetShape(freeShapes.Value(i));
      if (!s.IsNull()) builder.Add(compound, s);
    }
    shape = compound;
  }
  if (shape.IsNull())
    throw std::runtime_error("Failed to get shape from XCAF");

  std::string shape_id = "shape_" + std::to_string(pImpl->next_id++);
  pImpl->shapes[shape_id]          = shape;
  pImpl->docs[shape_id]            = doc;
  pImpl->shape_id_by_path[filepath] = shape_id; // for repair path lookup

  PartTreeData partTree = ExtractPartTree(doc, shapeTool, colorTool);

  // Build a set of face TShape addresses from OVER_RIDING_STYLED_ITEM entities via the
  // STEP reader transfer map. This is the reliable fallback for assembly files where
  // XCAF's colorTool cannot resolve per-face colors through component references.
  std::unordered_set<Standard_Address> styledFaceAddrs;
  try {
    Handle(XSControl_WorkSession) WS = reader.Reader().WS();
    Handle(Transfer_TransientProcess) TP = WS->TransferReader()->TransientProcess();
    Handle(Interface_InterfaceModel) mdl = WS->Model();
    Standard_Integer nb = mdl->NbEntities();
    for (Standard_Integer i = 1; i <= nb; i++) {
      Handle(Standard_Transient) ent = mdl->Value(i);
      Handle(StepVisual_OverRidingStyledItem) orsi =
          Handle(StepVisual_OverRidingStyledItem)::DownCast(ent);
      if (orsi.IsNull()) continue;
      Handle(StepRepr_RepresentationItem) item = orsi->Item();
      if (item.IsNull()) continue;
      Handle(Transfer_Binder) binder = TP->Find(item);
      if (binder.IsNull()) continue;
      Handle(TransferBRep_ShapeBinder) sb =
          Handle(TransferBRep_ShapeBinder)::DownCast(binder);
      if (sb.IsNull() || sb->Result().IsNull()) continue;
      styledFaceAddrs.insert((Standard_Address)sb->Result().TShape().get());
    }
  } catch (...) {}

  // HOOPS Exchange compatibility: if a solid has partial MDGPR face-color coverage,
  // split it into 2 virtual sub-parts so the viewer shows the same "2 sheets" split
  // that Creo/Keyshot would see — giving the user a clear visual reason to fix the file.
  {
    size_t origCount = partTree.parts.size();
    for (size_t i = 0; i < origCount; i++) {
      PartNode& part = partTree.parts[i];
      if (!part.childIds.empty()) continue; // skip non-leaves
      TopoDS_Shape partShape = partTree.shapes[part.id];
      if (partShape.IsNull()) continue;
      // Only try to split SOLID (or COMPOUND of faces) shapes
      if (partShape.ShapeType() != TopAbs_SOLID &&
          partShape.ShapeType() != TopAbs_SHELL &&
          partShape.ShapeType() != TopAbs_COMPOUND) continue;

      TopoDS_Shape covShape, uncShape;
      Quantity_Color covColor;
      if (!SplitSolidByFaceColor(partShape, colorTool, styledFaceAddrs, covShape, covColor, uncShape)) continue;

      // Count faces in each group for the name
      int covCount = 0, uncCount = 0;
      for (TopExp_Explorer fe(covShape, TopAbs_FACE); fe.More(); fe.Next()) covCount++;
      for (TopExp_Explorer fe(uncShape, TopAbs_FACE); fe.More(); fe.Next()) uncCount++;

      // Make the original part a group (no direct mesh)
      std::string parentId = part.id;
      part.isAssembly = false; // keep as false but add children

      // Child 1: styled/covered faces — faces that import apps can see.
      // Named after the parent PRODUCT ("0") so the tree mirrors what other apps
      // (Keyshot, Creo) display for the two sheets before repair.
      PartNode cov;
      cov.id = parentId + "_styled";
      cov.parentId = parentId;
      cov.name = part.name + " (" + std::to_string(covCount) + " faces)";
      cov.shapeType = "FACE";
      cov.isAssembly = false;
      cov.hasColor = true;
      cov.color[0] = (float)covColor.Red();
      cov.color[1] = (float)covColor.Green();
      cov.color[2] = (float)covColor.Blue();
      cov.startVertex = cov.vertexCount = cov.startIndex = cov.indexCount = 0;
      cov.startEdgePoint = cov.edgePointCount = cov.startEdgeIndex = cov.edgeIndexCount = 0;
      partTree.shapes[cov.id] = covShape;

      // Child 2: unstyled/uncovered faces — these appear as a "2nd sheet" to import apps.
      // Same naming convention: PRODUCT name + face count + repair hint.
      PartNode unc;
      unc.id = parentId + "_unstyled";
      unc.parentId = parentId;
      unc.name = part.name + " (" + std::to_string(uncCount) + " faces — needs repair)";
      unc.shapeType = "FACE";
      unc.isAssembly = false;
      // Use a warning orange so it stands out as the problem group
      unc.hasColor = true;
      unc.color[0] = 0.95f; unc.color[1] = 0.45f; unc.color[2] = 0.10f;
      unc.startVertex = unc.vertexCount = unc.startIndex = unc.indexCount = 0;
      unc.startEdgePoint = unc.edgePointCount = unc.startEdgeIndex = unc.edgeIndexCount = 0;
      partTree.shapes[unc.id] = uncShape;

      part.childIds.push_back(cov.id);
      part.childIds.push_back(unc.id);
      partTree.parts.push_back(cov);
      partTree.parts.push_back(unc);
    }
  }

  // BREP SOLID NAMES: Read MANIFOLD_SOLID_BREP entity names from the STEP model.
  // For leaf parts that were NOT split by the HOOPS compat step above, we create
  // a named child node so the tree mirrors what Plasticity shows after repair
  // (e.g. a "Solid 262.001" leaf under the "test slider3.stp" component).
  {
    std::unordered_map<Standard_Address, std::string> brepNameMap;
    try {
      Handle(XSControl_WorkSession) WS = reader.Reader().WS();
      Handle(Transfer_TransientProcess) TP = WS->TransferReader()->TransientProcess();
      Handle(Interface_InterfaceModel) mdl = WS->Model();
      Standard_Integer nb = mdl->NbEntities();
      for (Standard_Integer i = 1; i <= nb; i++) {
        Handle(Standard_Transient) ent = mdl->Value(i);
        Handle(StepShape_ManifoldSolidBrep) msb =
            Handle(StepShape_ManifoldSolidBrep)::DownCast(ent);
        if (msb.IsNull()) continue;
        Handle(TCollection_HAsciiString) bName = msb->Name();
        if (bName.IsNull() || bName->IsEmpty()) continue;
        std::string n = bName->ToCString();
        if (n.empty() || n == "0" || n == " ") continue;
        Handle(Transfer_Binder) binder = TP->Find(ent);
        if (binder.IsNull()) continue;
        Handle(TransferBRep_ShapeBinder) sb =
            Handle(TransferBRep_ShapeBinder)::DownCast(binder);
        if (sb.IsNull() || sb->Result().IsNull()) continue;
        brepNameMap[(Standard_Address)sb->Result().TShape().get()] = n;
      }
    } catch (...) {}

    // Cache for reuse by RepairNames in the text-level repair path
    // (WorkSession is gone after LoadStepMesh returns).
    pImpl->brep_name_maps[shape_id] = brepNameMap;

    if (!brepNameMap.empty()) {
      size_t brepOrigCount = partTree.parts.size();
      for (size_t i = 0; i < brepOrigCount; i++) {
        PartNode& part = partTree.parts[i];
        if (!part.childIds.empty()) continue; // already split (e.g. styled/unstyled)
        auto shapeIt = partTree.shapes.find(part.id);
        if (shapeIt == partTree.shapes.end() || shapeIt->second.IsNull()) continue;
        Standard_Address addr =
            (Standard_Address)shapeIt->second.TShape().get();
        auto nameIt = brepNameMap.find(addr);
        if (nameIt == brepNameMap.end()) continue;
        const std::string& brepName = nameIt->second;
        if (brepName == part.name) continue; // name already correct, no sub-node needed

        // Create a child node carrying the BREP entity name (e.g. "Solid 262.001").
        // The parent becomes a folder; the child carries the actual geometry.
        PartNode child;
        child.id = part.id + "_solid";
        child.parentId = part.id;
        child.name = brepName;
        child.shapeType = part.shapeType;
        child.isAssembly = false;
        child.hasColor = part.hasColor;
        memcpy(child.color, part.color, sizeof(child.color));
        child.startVertex = child.vertexCount = child.startIndex = child.indexCount = 0;
        child.startEdgePoint = child.edgePointCount =
            child.startEdgeIndex = child.edgeIndexCount = 0;
        partTree.shapes[child.id] = shapeIt->second; // same geometry
        part.childIds.push_back(child.id);
        partTree.parts.push_back(child);
      }
    }
  }

  // ROOT NAME: Replace the generic 'root' product name with the file's basename
  // so the top-level tree node matches what other apps (KeyShot, Plasticity) display.
  {
    std::string filename = filepath;
    size_t sep = filename.find_last_of("/\\");
    if (sep != std::string::npos) filename = filename.substr(sep + 1);
    for (PartNode& part : partTree.parts) {
      if (part.parentId.empty() && part.name == "root")
        part.name = filename;
    }
  }

  pImpl->part_trees[shape_id] = partTree;

  // Count leaf parts (the ones that need tessellation)
  int leafCount = 0;
  for (const auto& p : partTree.parts)
    if (!p.isAssembly && p.childIds.empty()) leafCount++;
  LOG_AND_STORE(pImpl, "[StepViewer] Part tree: " << partTree.parts.size() << " parts (" << leafCount << " solids)");

  double linear = GetLinearDeflection(quality);
  double angular = GetAngularDeflection(quality);
  LOG_AND_STORE(pImpl, "Tessellating " << leafCount << " solid" << (leafCount != 1 ? "s" : "") << "...");
  {
    auto tTess = std::chrono::high_resolution_clock::now();
    // Tessellate the whole compound in one parallel pass.
    // BRepMesh_IncrementalMesh with isParallel=true distributes face work across
    // all CPU cores. All per-part shapes share the same underlying TShape
    // pointers, so they will find triangulation already present when we iterate
    // below — no need to re-tessellate per part.
    TessellateShape(shape, linear, angular);
    auto msTess = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - tTess).count();
    LOG_AND_STORE(pImpl, "Tessellation done in " << msTess << "ms — extracting mesh...");
  }

  MeshData unifiedMesh;
  EdgeData unifiedEdges;
  uint32_t gv = 0, gi = 0, gev = 0;
  int totalFaces = 0, totalTri = 0, totalEdges = 0;
  int partIdx = 0;

  for (PartNode& part : partTree.parts) {
    if (part.isAssembly || !part.childIds.empty()) continue;
    TopoDS_Shape partShape = partTree.shapes[part.id];
    if (partShape.IsNull()) continue;
    partIdx++;
    LOG_AND_STORE(pImpl, "[" << partIdx << "/" << leafCount << "] " << part.name);
    // No TessellateShape here — faces are already triangulated from the
    // compound pass above. Calling it again per-part would create N
    // BRepMesh_IncrementalMesh objects that immediately skip every face.
    MeshData partMesh;
    ExtractMesh(partShape, partMesh);
    EdgeData partEdges;
    ExtractEdges(partShape, partEdges, quality);

    part.startVertex = gv;
    part.vertexCount = (uint32_t)(partMesh.positions.size() / 3);
    part.startIndex = gi;
    part.indexCount = (uint32_t)partMesh.indices.size();
    part.startEdgePoint = gev;
    part.edgePointCount = (uint32_t)(partEdges.positions.size() / 3);
    part.startEdgeIndex = (uint32_t)(unifiedEdges.indices.size() / 2);
    part.edgeIndexCount = (uint32_t)(partEdges.indices.size() / 2);

    unifiedMesh.positions.insert(unifiedMesh.positions.end(), partMesh.positions.begin(), partMesh.positions.end());
    unifiedMesh.normals.insert(unifiedMesh.normals.end(), partMesh.normals.begin(), partMesh.normals.end());
    for (uint32_t idx : partMesh.indices)
      unifiedMesh.indices.push_back(idx + gv);
    unifiedEdges.positions.insert(unifiedEdges.positions.end(), partEdges.positions.begin(), partEdges.positions.end());
    for (size_t i = 0; i < partEdges.indices.size(); i += 2) {
      unifiedEdges.indices.push_back(partEdges.indices[i] + gev);
      unifiedEdges.indices.push_back(partEdges.indices[i + 1]);
    }

    gv += part.vertexCount;
    gi += part.indexCount;
    gev += part.edgePointCount;
    totalFaces += partMesh.face_count;
    totalTri += partMesh.triangle_count;
    totalEdges += partEdges.edge_count;
  }

  unifiedMesh.face_count = totalFaces;
  unifiedMesh.triangle_count = totalTri;
  unifiedEdges.edge_count = totalEdges;
  ComputeBoundingBox(shape, unifiedMesh.bbox_min, unifiedMesh.bbox_max);
  unifiedMesh.logs = pImpl->logs;

  auto end = std::chrono::high_resolution_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  LOG_AND_STORE(pImpl, "[StepViewer] Done in " << ms << "ms: " << gv << " vertices, " << totalTri << " triangles");

  LoadResult result;
  result.shape_id = shape_id;
  result.mesh = std::move(unifiedMesh);
  result.edges = std::move(unifiedEdges);
  result.part_tree = partTree;
  return result;
}

TopoDS_Shape StepViewer::GetShape(const std::string& shape_id) {
  auto it = pImpl->shapes.find(shape_id);
  if (it == pImpl->shapes.end()) throw std::runtime_error("Shape not found: " + shape_id);
  return it->second;
}

Handle(TDocStd_Document) StepViewer::GetDocument(const std::string& shape_id) {
  auto it = pImpl->docs.find(shape_id);
  if (it == pImpl->docs.end()) return nullptr;
  return it->second;
}

std::string StepViewer::GetRawContent(const std::string& filepath) const {
  auto it = pImpl->raw_by_path.find(filepath);
  return (it != pImpl->raw_by_path.end()) ? it->second : std::string{};
}

Handle(TDocStd_Document) StepViewer::GetDocumentForPath(const std::string& filepath) const {
  auto idIt = pImpl->shape_id_by_path.find(filepath);
  if (idIt == pImpl->shape_id_by_path.end()) return nullptr;
  auto docIt = pImpl->docs.find(idIt->second);
  return (docIt != pImpl->docs.end()) ? docIt->second : nullptr;
}

void StepViewer::InvalidatePath(const std::string& filepath) {
  pImpl->raw_by_path.erase(filepath);
  pImpl->shape_id_by_path.erase(filepath);
}

std::unordered_map<Standard_Address, std::string>
StepViewer::GetBrepNameMap(const std::string& filepath) const {
  auto idIt = pImpl->shape_id_by_path.find(filepath);
  if (idIt == pImpl->shape_id_by_path.end())
    return {};
  auto mapIt = pImpl->brep_name_maps.find(idIt->second);
  return (mapIt != pImpl->brep_name_maps.end()) ? mapIt->second
                                                 : std::unordered_map<Standard_Address, std::string>{};
}

LoadResult StepViewer::RebuildFromCachedShape(
    const std::string& origFilepath,
    const std::string& outputFilepath,
    const std::string& quality)
{
  // Look up the original shape and doc via the filepath cache
  auto idIt = pImpl->shape_id_by_path.find(origFilepath);
  if (idIt == pImpl->shape_id_by_path.end())
    throw std::runtime_error("RebuildFromCachedShape: no cached shape for " + origFilepath);

  const std::string& origId = idIt->second;

  auto shapeIt = pImpl->shapes.find(origId);
  if (shapeIt == pImpl->shapes.end())
    throw std::runtime_error("RebuildFromCachedShape: shape not found for " + origId);
  const TopoDS_Shape& topShape = shapeIt->second;

  auto docIt = pImpl->docs.find(origId);
  if (docIt == pImpl->docs.end())
    throw std::runtime_error("RebuildFromCachedShape: doc not found for " + origId);
  Handle(TDocStd_Document) doc = docIt->second;

  Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

  // Re-extract part tree from the modified doc (names already repaired, face
  // colors already stripped by the caller).  No styledFaceAddrs split and no
  // MSB sub-nodes needed — the repaired doc has correct names and no OVER_RIDING
  // items, so the tree is already clean.
  PartTreeData partTree = ExtractPartTree(doc, shapeTool, colorTool);

  // Replace "root" with the output file's basename so the tree node shows the
  // repaired file name rather than the generic XCAF root product name.
  {
    std::string filename = outputFilepath;
    size_t sep = filename.find_last_of("/\\");
    if (sep != std::string::npos) filename = filename.substr(sep + 1);
    for (PartNode& part : partTree.parts)
      if (part.parentId.empty() && part.name == "root")
        part.name = filename;
  }

  std::string shape_id = "shape_" + std::to_string(pImpl->next_id++);
  pImpl->shapes[shape_id]           = topShape;
  pImpl->docs[shape_id]             = doc;
  pImpl->part_trees[shape_id]       = partTree;
  pImpl->shape_id_by_path[outputFilepath] = shape_id;

  // Re-extract mesh and edges from the already-tessellated TShapes.
  // BRepMesh_IncrementalMesh stored the triangulation on the TShape objects
  // during the original LoadStepMesh call.  ExtractMesh/ExtractEdges simply
  // read that stored data — no recomputation happens, so this is fast.
  int leafCount = 0;
  for (const auto& p : partTree.parts)
    if (!p.isAssembly && p.childIds.empty()) leafCount++;

  MeshData unifiedMesh;
  EdgeData  unifiedEdges;
  uint32_t  gv = 0, gi = 0, gev = 0;
  int totalFaces = 0, totalTri = 0, totalEdges = 0;

  for (PartNode& part : partTree.parts) {
    if (part.isAssembly || !part.childIds.empty()) continue;
    TopoDS_Shape partShape = partTree.shapes[part.id];
    if (partShape.IsNull()) continue;

    MeshData partMesh;
    ExtractMesh(partShape, partMesh);
    EdgeData partEdges;
    ExtractEdges(partShape, partEdges, quality);

    part.startVertex    = gv;
    part.vertexCount    = (uint32_t)(partMesh.positions.size() / 3);
    part.startIndex     = gi;
    part.indexCount     = (uint32_t)partMesh.indices.size();
    part.startEdgePoint = gev;
    part.edgePointCount = (uint32_t)(partEdges.positions.size() / 3);
    part.startEdgeIndex = (uint32_t)(unifiedEdges.indices.size() / 2);
    part.edgeIndexCount = (uint32_t)(partEdges.indices.size() / 2);

    unifiedMesh.positions.insert(unifiedMesh.positions.end(),
        partMesh.positions.begin(), partMesh.positions.end());
    unifiedMesh.normals.insert(unifiedMesh.normals.end(),
        partMesh.normals.begin(), partMesh.normals.end());
    for (uint32_t idx : partMesh.indices)
      unifiedMesh.indices.push_back(idx + gv);

    unifiedEdges.positions.insert(unifiedEdges.positions.end(),
        partEdges.positions.begin(), partEdges.positions.end());
    for (size_t i = 0; i < partEdges.indices.size(); i += 2) {
      unifiedEdges.indices.push_back(partEdges.indices[i] + gev);
      unifiedEdges.indices.push_back(partEdges.indices[i + 1]);
    }

    gv  += part.vertexCount;
    gi  += part.indexCount;
    gev += part.edgePointCount;
    totalFaces  += partMesh.face_count;
    totalTri    += partMesh.triangle_count;
    totalEdges  += partEdges.edge_count;
  }

  unifiedMesh.face_count     = totalFaces;
  unifiedMesh.triangle_count = totalTri;
  unifiedEdges.edge_count    = totalEdges;
  ComputeBoundingBox(topShape, unifiedMesh.bbox_min, unifiedMesh.bbox_max);

  LoadResult res;
  res.shape_id   = shape_id;
  res.mesh       = std::move(unifiedMesh);
  res.edges      = std::move(unifiedEdges);
  res.part_tree  = partTree;
  return res;
}

} // namespace StepFixerNative
