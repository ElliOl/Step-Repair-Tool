/**
 * StepFixer Native Addon - N-API bindings for analyseStep, repairStep, loadStepMesh
 */

#include <napi.h>
#include <string>
#include <unordered_map>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <Standard_Version.hxx>
#include <Standard_TypeDef.hxx>
#include <XSControl_WorkSession.hxx>
#include <XSControl_TransferReader.hxx>
#include <Transfer_TransientProcess.hxx>
#include <Interface_InterfaceModel.hxx>
#include <TransferBRep_ShapeBinder.hxx>
#include <StepShape_ManifoldSolidBrep.hxx>
#include <TCollection_HAsciiString.hxx>

#include "step_viewer.h"
#include "name_repair.h"
#include "shell_split.h"
#include "hoops_compat.h"
#include <XCAFDoc_ColorTool.hxx>

static StepFixerNative::StepViewer g_viewer;

static void MarshalLoadResult(Napi::Env env, const StepFixerNative::LoadResult& r, Napi::Object& result) {
  result.Set("shapeId", Napi::String::New(env, r.shape_id));

  Napi::Object meshObj = Napi::Object::New(env);
  Napi::ArrayBuffer posBuf = Napi::ArrayBuffer::New(env, r.mesh.positions.size() * sizeof(float));
  memcpy(posBuf.Data(), r.mesh.positions.data(), r.mesh.positions.size() * sizeof(float));
  meshObj.Set("positions", Napi::Float32Array::New(env, r.mesh.positions.size(), posBuf, 0));
  Napi::ArrayBuffer normBuf = Napi::ArrayBuffer::New(env, r.mesh.normals.size() * sizeof(float));
  memcpy(normBuf.Data(), r.mesh.normals.data(), r.mesh.normals.size() * sizeof(float));
  meshObj.Set("normals", Napi::Float32Array::New(env, r.mesh.normals.size(), normBuf, 0));
  Napi::ArrayBuffer idxBuf = Napi::ArrayBuffer::New(env, r.mesh.indices.size() * sizeof(uint32_t));
  memcpy(idxBuf.Data(), r.mesh.indices.data(), r.mesh.indices.size() * sizeof(uint32_t));
  meshObj.Set("indices", Napi::Uint32Array::New(env, r.mesh.indices.size(), idxBuf, 0));
  Napi::Array bboxMin = Napi::Array::New(env, 3);
  for (int i = 0; i < 3; i++) bboxMin.Set(i, Napi::Number::New(env, r.mesh.bbox_min[i]));
  meshObj.Set("bboxMin", bboxMin);
  Napi::Array bboxMax = Napi::Array::New(env, 3);
  for (int i = 0; i < 3; i++) bboxMax.Set(i, Napi::Number::New(env, r.mesh.bbox_max[i]));
  meshObj.Set("bboxMax", bboxMax);
  meshObj.Set("faceCount", Napi::Number::New(env, r.mesh.face_count));
  meshObj.Set("triangleCount", Napi::Number::New(env, r.mesh.triangle_count));
  result.Set("mesh", meshObj);

  Napi::Object edgesObj = Napi::Object::New(env);
  Napi::ArrayBuffer edgePosBuf = Napi::ArrayBuffer::New(env, r.edges.positions.size() * sizeof(float));
  memcpy(edgePosBuf.Data(), r.edges.positions.data(), r.edges.positions.size() * sizeof(float));
  edgesObj.Set("positions", Napi::Float32Array::New(env, r.edges.positions.size(), edgePosBuf, 0));
  Napi::ArrayBuffer edgeIdxBuf = Napi::ArrayBuffer::New(env, r.edges.indices.size() * sizeof(uint32_t));
  memcpy(edgeIdxBuf.Data(), r.edges.indices.data(), r.edges.indices.size() * sizeof(uint32_t));
  edgesObj.Set("indices", Napi::Uint32Array::New(env, r.edges.indices.size(), edgeIdxBuf, 0));
  edgesObj.Set("edgeCount", Napi::Number::New(env, r.edges.edge_count));
  result.Set("edges", edgesObj);

  Napi::Array partsArr = Napi::Array::New(env, r.part_tree.parts.size());
  for (size_t i = 0; i < r.part_tree.parts.size(); i++) {
    const auto& p = r.part_tree.parts[i];
    Napi::Object po = Napi::Object::New(env);
    po.Set("id", Napi::String::New(env, p.id));
    po.Set("name", Napi::String::New(env, p.name));
    po.Set("instanceName", Napi::String::New(env, p.instanceName));
    po.Set("parentId", p.parentId.empty() ? env.Null() : Napi::String::New(env, p.parentId));
    Napi::Array children = Napi::Array::New(env, p.childIds.size());
    for (size_t j = 0; j < p.childIds.size(); j++) children.Set(j, Napi::String::New(env, p.childIds[j]));
    po.Set("children", children);
    if (p.hasColor) {
      Napi::Array col = Napi::Array::New(env, 3);
      col.Set(0u, Napi::Number::New(env, p.color[0]));
      col.Set(1u, Napi::Number::New(env, p.color[1]));
      col.Set(2u, Napi::Number::New(env, p.color[2]));
      po.Set("color", col);
    } else {
      po.Set("color", env.Null());
    }
    po.Set("shapeType", Napi::String::New(env, p.shapeType));
    po.Set("isAssembly", Napi::Boolean::New(env, p.isAssembly));
    po.Set("startVertex", Napi::Number::New(env, p.startVertex));
    po.Set("vertexCount", Napi::Number::New(env, p.vertexCount));
    po.Set("startIndex", Napi::Number::New(env, p.startIndex));
    po.Set("indexCount", Napi::Number::New(env, p.indexCount));
    po.Set("startEdgePoint", Napi::Number::New(env, p.startEdgePoint));
    po.Set("edgePointCount", Napi::Number::New(env, p.edgePointCount));
    po.Set("startEdgeIndex", Napi::Number::New(env, p.startEdgeIndex));
    po.Set("edgeIndexCount", Napi::Number::New(env, p.edgeIndexCount));
    partsArr.Set(i, po);
  }
  result.Set("parts", partsArr);
}

Napi::Value Health(const Napi::CallbackInfo& info) {
  return Napi::Boolean::New(info.Env(), true);
}

Napi::Value GetVersion(const Napi::CallbackInfo& info) {
  return Napi::String::New(info.Env(), std::string("0.1.0 (OCCT ") + OCC_VERSION_STRING_EXT + ")");
}

class AnalyseStepWorker : public Napi::AsyncWorker {
public:
  AnalyseStepWorker(const Napi::Env& env, const std::string& filepath, const std::string& quality, Napi::Function logCb)
    : Napi::AsyncWorker(env), deferred(Napi::Promise::Deferred::New(env)), filepath(filepath), quality(quality) {
    if (!logCb.IsEmpty() && !logCb.IsNull() && !logCb.IsUndefined()) {
      tsfn = Napi::ThreadSafeFunction::New(env, logCb, "AnalyseLog", 0, 1);
      hasCallback = true;
    }
  }
  void Execute() override {
    try {
      if (hasCallback) {
        g_viewer.SetLogCallback([this](const std::string& msg) {
          tsfn.BlockingCall([msg](Napi::Env env, Napi::Function cb) { cb.Call({Napi::String::New(env, msg)}); });
        });
      }
      result = g_viewer.LoadStepMesh(filepath, quality);
      g_viewer.SetLogCallback(nullptr);
      Handle(TDocStd_Document) doc = g_viewer.GetDocument(result.shape_id);
      Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
      namesFlagged = StepFixerNative::CountNamesToRepair(doc, shapeTool);
      shellsSplit = StepFixerNative::CountSolidsToSplit(doc, shapeTool);
      hoopsCompatFixes = StepFixerNative::CountHoopsCompatFixes(filepath);
    } catch (const std::exception& e) {
      g_viewer.SetLogCallback(nullptr);
      SetError(e.what());
    }
    if (hasCallback) tsfn.Release();
  }
  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object out = Napi::Object::New(env);
    out.Set("namesFlagged", Napi::Number::New(env, namesFlagged));
    out.Set("shellsSplit", Napi::Number::New(env, shellsSplit));
    out.Set("hoopsCompatFixes", Napi::Number::New(env, hoopsCompatFixes));
    MarshalLoadResult(env, result, out);
    deferred.Resolve(out);
  }
  void OnError(const Napi::Error& err) override {
    deferred.Reject(err.Value());
  }
  Napi::Promise GetPromise() { return deferred.Promise(); }
private:
  Napi::Promise::Deferred deferred;
  std::string filepath, quality;
  StepFixerNative::LoadResult result;
  int namesFlagged = 0, shellsSplit = 0, hoopsCompatFixes = 0;
  Napi::ThreadSafeFunction tsfn;
  bool hasCallback = false;
};

Napi::Value AnalyseStep(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected (filepath: string, quality?: string, logCallback?: Function)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string filepath = info[0].As<Napi::String>().Utf8Value();
  std::string quality = info.Length() >= 2 && info[1].IsString() ? info[1].As<Napi::String>().Utf8Value() : "standard";
  Napi::Function logCb = info.Length() >= 3 && info[2].IsFunction() ? info[2].As<Napi::Function>() : Napi::Function();
  AnalyseStepWorker* w = new AnalyseStepWorker(env, filepath, quality, logCb);
  w->Queue();
  return w->GetPromise();
}

class RepairStepWorker : public Napi::AsyncWorker {
public:
  RepairStepWorker(const Napi::Env& env,
    const std::string& filepath,
    const std::string& outputPath,
    bool fixNames,
    bool fixShells,
    bool fixHoopsCompat,
    Napi::Function& logCb)
    : Napi::AsyncWorker(env),
      deferred(Napi::Promise::Deferred::New(env)),
      filepath(filepath),
      outputPath(outputPath),
      fixNames(fixNames),
      fixShells(fixShells),
      fixHoopsCompat(fixHoopsCompat) {
    tsfn = Napi::ThreadSafeFunction::New(env, logCb, "RepairLog", 0, 1);
  }
  void Execute() override {
    try {
      g_viewer.SetLogCallback([this](const std::string& msg) {
        tsfn.BlockingCall([msg](Napi::Env env, Napi::Function cb) { cb.Call({Napi::String::New(env, msg)}); });
      });
      Handle(TDocStd_Document) doc = new TDocStd_Document("MDTV-XCAF");
      STEPCAFControl_Reader reader;
      reader.SetColorMode(Standard_True);
      reader.SetNameMode(Standard_True);
      reader.SetLayerMode(Standard_True);
      if (reader.ReadFile(filepath.c_str()) != IFSelect_RetDone)
        throw std::runtime_error("Failed to read STEP file");
      if (!reader.Transfer(doc))
        throw std::runtime_error("Failed to transfer to XCAF");
      Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
      Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

      // Build a map of TShape address → MSB entity name from the original file.
      // RepairNames uses this to prefer the Plasticity body name (e.g. "Solid 262.001")
      // stored in MANIFOLD_SOLID_BREP over the NAUO instance name, which for
      // assembly-embedded files may be a file-path like "test slider3.stp".
      std::unordered_map<Standard_Address, std::string> brepNameMap;
      if (fixNames) {
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
            Handle(Transfer_Binder) binder = TP->Find(msb);
            if (binder.IsNull()) continue;
            Handle(TransferBRep_ShapeBinder) sb =
                Handle(TransferBRep_ShapeBinder)::DownCast(binder);
            if (sb.IsNull() || sb->Result().IsNull()) continue;
            brepNameMap[(Standard_Address)sb->Result().TShape().get()] = n;
          }
        } catch (...) {}
      }

      if (fixNames) StepFixerNative::RepairNames(doc, shapeTool, brepNameMap);
      if (fixShells) StepFixerNative::SplitDisconnectedShellsInDocument(doc, shapeTool);
      if (fixHoopsCompat) StepFixerNative::StripPerFaceColors(doc, shapeTool, colorTool);
      STEPCAFControl_Writer writer;
      if (!writer.Perform(doc, outputPath.c_str()))
        throw std::runtime_error("Failed to write STEP file");
      result = g_viewer.LoadStepMesh(outputPath, "standard");
    } catch (const std::exception& e) { SetError(e.what()); }
    tsfn.Release();
  }
  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object out = Napi::Object::New(env);
    out.Set("success", Napi::Boolean::New(env, true));
    Napi::Array logArr = Napi::Array::New(env, result.mesh.logs.size());
    for (size_t i = 0; i < result.mesh.logs.size(); i++)
      logArr.Set(i, Napi::String::New(env, result.mesh.logs[i]));
    out.Set("log", logArr);
    MarshalLoadResult(env, result, out);
    deferred.Resolve(out);
  }
  void OnError(const Napi::Error& err) override {
    deferred.Reject(err.Value());
  }
  Napi::Promise GetPromise() { return deferred.Promise(); }
private:
  Napi::Promise::Deferred deferred;
  Napi::ThreadSafeFunction tsfn;
  std::string filepath, outputPath;
  bool fixNames, fixShells, fixHoopsCompat;
  StepFixerNative::LoadResult result;
};

Napi::Value RepairStep(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 4 || !info[0].IsString() || !info[1].IsString() || !info[2].IsObject() || !info[3].IsFunction()) {
    Napi::TypeError::New(env, "Expected (filepath, outputPath, options: { fixNames, fixShells }, logCallback)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string filepath = info[0].As<Napi::String>().Utf8Value();
  std::string outputPath = info[1].As<Napi::String>().Utf8Value();
  Napi::Object opts = info[2].As<Napi::Object>();
  bool fixNames = opts.Get("fixNames").As<Napi::Boolean>().Value();
  bool fixShells = opts.Get("fixShells").As<Napi::Boolean>().Value();
  bool fixHoopsCompat = opts.Has("fixHoopsCompat") && opts.Get("fixHoopsCompat").As<Napi::Boolean>().Value();
  Napi::Function logCb = info[3].As<Napi::Function>();
  RepairStepWorker* w = new RepairStepWorker(env, filepath, outputPath, fixNames, fixShells, fixHoopsCompat, logCb);
  w->Queue();
  return w->GetPromise();
}

class LoadStepMeshWorker : public Napi::AsyncWorker {
public:
  LoadStepMeshWorker(const Napi::Env& env, const std::string& filepath, const std::string& quality)
    : Napi::AsyncWorker(env), deferred(Napi::Promise::Deferred::New(env)), filepath(filepath), quality(quality) {}
  void Execute() override {
    try {
      result = g_viewer.LoadStepMesh(filepath, quality);
    } catch (const std::exception& e) { SetError(e.what()); }
  }
  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object out = Napi::Object::New(env);
    MarshalLoadResult(env, result, out);
    deferred.Resolve(out);
  }
  Napi::Promise GetPromise() { return deferred.Promise(); }
private:
  Napi::Promise::Deferred deferred;
  std::string filepath, quality;
  StepFixerNative::LoadResult result;
};

Napi::Value LoadStepMesh(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected (filepath: string, quality?: string)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string filepath = info[0].As<Napi::String>().Utf8Value();
  std::string quality = info.Length() >= 2 && info[1].IsString() ? info[1].As<Napi::String>().Utf8Value() : "standard";
  LoadStepMeshWorker* w = new LoadStepMeshWorker(env, filepath, quality);
  w->Queue();
  return w->GetPromise();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("health", Napi::Function::New(env, Health));
  exports.Set("getVersion", Napi::Function::New(env, GetVersion));
  exports.Set("analyseStep", Napi::Function::New(env, AnalyseStep));
  exports.Set("repairStep", Napi::Function::New(env, RepairStep));
  exports.Set("loadStepMesh", Napi::Function::New(env, LoadStepMesh));
  return exports;
}

NODE_API_MODULE(step_fixer_native, Init)
