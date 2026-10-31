#include "StepLoader.h"

#include "ExtraFormats.h"
#include "Translate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <Geom_Surface.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Message_ProgressScope.hxx>
#include <OSD_Parallel.hxx>
#include <ShapeProcess.hxx>
#include <TopExp.hxx>
#include <XSAlgo_ShapeProcessor.hxx>
#include <ShapeProcess_ShapeContext.hxx>
#include <unordered_map>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <RWObj_CafReader.hxx>
#include <RWStl.hxx>
#include <Precision.hxx>
#include <Quantity_Color.hxx>
#include <RWGltf_CafReader.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <VrmlAPI_CafReader.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace {

using opencascade::handle;

struct Color {
    float r = 0.72f, g = 0.74f, b = 0.78f;
    bool valid = false;
};

// Details about a part that come from the file's own project data (Bambu/Orca), keyed by the mesh label's tag.
struct PartMeta {
    std::string object;
    std::vector<FilamentUse> filaments;
};

// ---- Progress and cancelling (LoadProgress is read by the window)

void setStage(LoadProgress* p, LoadStage stage, float fraction) {
    if (!p) return;
    p->stage = int(stage);
    p->fraction = fraction;
}

void checkCancel(const LoadProgress* p) {
    if (p && p->cancel.load()) throw LoadCancelled();
}

// Passes OpenCASCADE's own progress on (as the part `from`..`to` of the whole load) and stops it when Cancel was pressed.
class LoadIndicator : public Message_ProgressIndicator {
public:
    LoadIndicator(LoadProgress* p, float from, float to) : p_(p), from_(from), to_(to) {}
    bool UserBreak() override { return p_ && p_->cancel.load(); }
    void Show(const Message_ProgressScope&, const bool) override {
        if (p_) p_->fraction = from_ + (to_ - from_) * float(std::clamp(GetPosition(), 0.0, 1.0));
    }

private:
    LoadProgress* p_;
    float from_, to_;
};

struct Builder {
    Model& model;
    LoadProgress* progress = nullptr;
    const std::map<int, PartMeta>* partMeta = nullptr;
    handle<XCAFDoc_ShapeTool> shapes;
    handle<XCAFDoc_ColorTool> colors;
    handle<XCAFDoc_VisMaterialTool> materials;  // mesh formats (e.g. OBJ) store colours as materials
    std::map<uint32_t, size_t> batchIndex;
    bool hasBounds = false;
    bool splitFaceCompounds = false;  // a compound made only of faces becomes one part per face
    std::map<int, TopoDS_Shape> healed;                   // see healParts: part label tag -> repaired shape
    std::unordered_map<const void*, Color> healedColors;  // see healParts: repaired face -> its own colour

    Builder(Model& m) : model(m) {}

    // OCCT stores colours as linear RGB; the viewport draws without gamma correction, so convert back to sRGB.
    static Color toDisplay(const Quantity_Color& c) {
        double r, g, b;
        c.Values(r, g, b, Quantity_TOC_sRGB);
        return {float(r), float(g), float(b), true};
    }

    bool labelColor(const TDF_Label& l, Color& out) const {
        Quantity_Color c;
        if (colors->GetColor(l, XCAFDoc_ColorSurf, c) || colors->GetColor(l, XCAFDoc_ColorGen, c)) {
            out = toDisplay(c);
            return true;
        }
        return materialColor(XCAFDoc_VisMaterialTool::GetShapeMaterial(l), out);
    }

    bool shapeColor(const TopoDS_Shape& s, Color& out) const {
        Quantity_Color c;
        if (colors->GetColor(s, XCAFDoc_ColorSurf, c) || colors->GetColor(s, XCAFDoc_ColorGen, c)) {
            out = toDisplay(c);
            return true;
        }
        return !materials.IsNull() && materialColor(materials->GetShapeMaterial(s), out);
    }

    bool isPartLabel(const TDF_Label& l) const { return l.Father() == shapes->Label(); }

    // STEP files are translated without OpenCASCADE's shape repair, because the repair runs on one core, one part after
    // another, and takes most of the load time. Here every distinct part is repaired with the same default repair, all
    // parts at once on all cores (parts share no edges). The face colours are read from the document first and handed
    // to the repaired faces, which are new objects. Returns the bounding box of the whole model.
    Bnd_Box healParts(const TDF_LabelSequence& roots) {
        struct Task {
            TDF_Label label;
            TopoDS_Shape shape, result;
            std::vector<std::pair<TopoDS_Shape, Color>> colours;      // before the repair
            std::vector<std::pair<const void*, Color>> healedColours;  // after the repair
            Bnd_Box box;
        };
        std::vector<Task> tasks;
        std::map<int, size_t> taskOf;
        std::function<void(const TDF_Label&)> gather = [&](const TDF_Label& l) {
            if (shapes->IsAssembly(l)) {
                TDF_LabelSequence comps;
                shapes->GetComponents(l, comps);
                for (int i = 1; i <= comps.Length(); ++i) {
                    TDF_Label ref;
                    if (!shapes->GetReferredShape(comps.Value(i), ref)) ref = comps.Value(i);
                    gather(ref);
                }
                return;
            }
            if (!isPartLabel(l) || taskOf.count(l.Tag())) return;
            Task t;
            t.label = l;
            t.shape = shapes->GetShape(l);
            if (t.shape.IsNull()) return;
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(t.shape, TopAbs_FACE, faces);
            for (int f = 1; f <= faces.Extent(); ++f) {
                TDF_Label sub;
                Color c;
                if (shapes->FindSubShape(l, faces(f), sub) && labelColor(sub, c)) t.colours.push_back({faces(f), c});
            }
            taskOf[l.Tag()] = tasks.size();
            tasks.push_back(std::move(t));
        };
        for (int i = 1; i <= roots.Length(); ++i) gather(roots.Value(i));

        auto defaults = XSAlgo_ShapeProcessor::ReadProcessingData("read.step.resource.name", "read.step.sequence");
        // The reader normally passes the precision of the file (in mm) here. Without it the repair behaves slightly differently
        // (a few faces failed to mesh); any value from 1e-7 to 1e-3 gave the same faces, so a fixed small one is used.
        defaults.first.Bind("Runtime.Tolerance", "1e-5");
        defaults.first.Bind("Runtime.MaxTolerance", "1");
        std::atomic<int> done{0};
        const float taskCount = float(std::max<size_t>(tasks.size(), 1));
        LoadProgress* const progressOut = progress;
        OSD_Parallel::ForEach(
            tasks.begin(), tasks.end(),
            [&defaults, &done, taskCount, progressOut](Task& t) {
                if (progressOut && progressOut->cancel.load()) {  // Cancel pressed: skip the remaining parts quickly
                    t.result = t.shape;
                    return;
                }
                if (progressOut) progressOut->fraction = 0.30f + 0.40f * float(done.load()) / taskCount;
                try {
                    XSAlgo_ShapeProcessor processor(defaults.first);
                    t.result = processor.ProcessShape(t.shape, defaults.second, Message_ProgressRange());
                    if (t.result.IsNull()) t.result = t.shape;
                    const auto context = processor.GetContext();
                    for (const auto& coloured : t.colours) {
                        TopoDS_Shape now = coloured.first;
                        if (!context.IsNull())  // the map is not recursive: follow it until the face is no longer replaced
                            for (int hop = 0; hop < 8 && context->Map().IsBound(now); ++hop) now = context->Map().Find(now);
                        for (TopExp_Explorer ex(now, TopAbs_FACE); ex.More(); ex.Next())
                            t.healedColours.push_back({ex.Current().TShape().get(), coloured.second});
                    }
                } catch (...) {  // a part that cannot be repaired is used as it is
                    t.result = t.shape;
                    t.healedColours.clear();
                    for (const auto& coloured : t.colours) t.healedColours.push_back({coloured.first.TShape().get(), coloured.second});
                }
                BRepBndLib::Add(t.result, t.box, false);
                ++done;
            },
            false, int(tasks.size()));
        checkCancel(progress);

        for (Task& t : tasks) {
            healed[t.label.Tag()] = t.result;
            for (const auto& hc : t.healedColours) healedColors[hc.first] = hc.second;
        }
        Bnd_Box total;
        std::function<void(const TDF_Label&, const TopLoc_Location&)> extend = [&](const TDF_Label& l,
                                                                                    const TopLoc_Location& loc) {
            if (shapes->IsAssembly(l)) {
                TDF_LabelSequence comps;
                shapes->GetComponents(l, comps);
                for (int i = 1; i <= comps.Length(); ++i) {
                    TDF_Label ref;
                    if (!shapes->GetReferredShape(comps.Value(i), ref)) ref = comps.Value(i);
                    extend(ref, loc * XCAFDoc_ShapeTool::GetLocation(comps.Value(i)));
                }
                return;
            }
            const auto it = isPartLabel(l) ? taskOf.find(l.Tag()) : taskOf.end();
            if (it != taskOf.end()) {
                if (!tasks[it->second].box.IsVoid()) total.Add(tasks[it->second].box.Transformed(loc.Transformation()));
            } else if (!shapes->GetShape(l).IsNull()) {
                BRepBndLib::Add(shapes->GetShape(l).Moved(loc), total, true);
            }
        };
        for (int i = 1; i <= roots.Length(); ++i) extend(roots.Value(i), TopLoc_Location());
        return total;
    }

    // All repaired parts as one shape, so they can be meshed together.
    TopoDS_Shape healedCompound() const {
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (const auto& h : healed) builder.Add(all, h.second);
        return all;
    }

    // Colour of one face of `shape` (the shape of `partLabel`). Searching the whole document for the face (shapeColor)
    // gets slower with every part, so a face is only looked up among the sub-shapes of its own part. A part that is
    // itself one face (mesh formats such as glTF) keeps the document-wide search: its colour can sit on another label.
    bool faceColor(const TDF_Label& partLabel, const TopoDS_Shape& shape, const TopoDS_Face& face, Color& out) const {
        if (!healedColors.empty()) {
            const auto it = healedColors.find(face.TShape().get());
            if (it != healedColors.end()) {
                out = it->second;
                return true;
            }
        }
        TDF_Label sub;
        if (shapes->FindSubShape(partLabel, face, sub)) return labelColor(sub, out);
        return face.IsSame(shape) && shapeColor(face, out);
    }

    static bool materialColor(const handle<XCAFDoc_VisMaterial>& m, Color& out) {
        if (m.IsNull() || m->IsEmpty()) return false;
        out = toDisplay(m->BaseColor().GetRGB());
        return true;
    }

    // Room for `extra` more elements. Plain reserve(size + extra) would reallocate and copy the whole vector for every
    // face, which made big models load in quadratic time, so the capacity at least doubles.
    template <class V>
    static void reserveMore(V& v, size_t extra) {
        if (v.capacity() < v.size() + extra) v.reserve(std::max(v.size() + extra, v.capacity() * 2));
    }

    MeshBatch& batchFor(const Color& c) {
        auto q = [](float v) { return uint32_t(std::lround(std::fmin(std::fmax(v, 0.f), 1.f) * 255.f)); };
        uint32_t key = (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
        auto it = batchIndex.find(key);
        if (it == batchIndex.end()) {
            it = batchIndex.emplace(key, model.batches.size()).first;
            model.batches.emplace_back();
            model.batches.back().color = {c.r, c.g, c.b};
        }
        return model.batches[it->second];
    }

    void grow(float x, float y, float z) {
        if (!hasBounds) {
            model.boundsMin = {x, y, z};
            model.boundsMax = {x, y, z};
            hasBounds = true;
            return;
        }
        model.boundsMin = {std::fmin(model.boundsMin[0], x), std::fmin(model.boundsMin[1], y),
                           std::fmin(model.boundsMin[2], z)};
        model.boundsMax = {std::fmax(model.boundsMax[0], x), std::fmax(model.boundsMax[1], y),
                           std::fmax(model.boundsMax[2], z)};
    }

    // Records the indices added to batch `b` since `idxStart` as part of the part being built.
    void addPartRange(const MeshBatch& b, size_t idxStart) {
        Part& part = model.parts.back();
        const int bi = int(&b - model.batches.data());
        const uint32_t added = uint32_t(b.indices.size() - idxStart);
        if (!part.ranges.empty() && part.ranges.back().batch == bi &&
            part.ranges.back().start + part.ranges.back().count == idxStart)
            part.ranges.back().count += added;
        else
            part.ranges.push_back({bi, uint32_t(idxStart), added});
    }

    // Mesh files (STL, OBJ, PLY, ...) have no surfaces to take normals from. Vertices are split per triangle and
    // each corner averages only the neighbouring triangles within a crease angle, so hard edges stay sharp.
    void addMeshOnlyFace(MeshBatch& b, const std::vector<float>& pos, const handle<Poly_Triangulation>& tri) {
        const int nNodes = tri->NbNodes(), nTri = tri->NbTriangles();
        std::vector<int> tv(size_t(nTri) * 3);
        std::vector<float> fn(size_t(nTri) * 3);  // area-weighted triangle normals
        std::vector<int> first(size_t(nNodes) + 2, 0);
        for (int t = 0; t < nTri; ++t) {
            int a, c, d;
            tri->Triangle(t + 1).Get(a, c, d);
            --a; --c; --d;
            tv[size_t(t) * 3] = a;
            tv[size_t(t) * 3 + 1] = c;
            tv[size_t(t) * 3 + 2] = d;
            const float* pa = &pos[size_t(a) * 3];
            const float* pc = &pos[size_t(c) * 3];
            const float* pd = &pos[size_t(d) * 3];
            const float u[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
            const float v[3] = {pd[0] - pa[0], pd[1] - pa[1], pd[2] - pa[2]};
            fn[size_t(t) * 3] = u[1] * v[2] - u[2] * v[1];
            fn[size_t(t) * 3 + 1] = u[2] * v[0] - u[0] * v[2];
            fn[size_t(t) * 3 + 2] = u[0] * v[1] - u[1] * v[0];
            for (int k = 0; k < 3; ++k) ++first[size_t(tv[size_t(t) * 3 + size_t(k)]) + 2];
        }
        for (size_t i = 2; i < first.size(); ++i) first[i] += first[i - 1];
        std::vector<int> incident(size_t(nTri) * 3);  // triangles around each node, grouped by node
        for (int t = 0; t < nTri; ++t)
            for (int k = 0; k < 3; ++k) incident[size_t(first[size_t(tv[size_t(t) * 3 + size_t(k)]) + 1]++)] = t;

        auto unit = [&](int t, float out[3]) {
            const float* n = &fn[size_t(t) * 3];
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0) { out[0] = n[0] / len; out[1] = n[1] / len; out[2] = n[2] / len; }
            else { out[0] = 0; out[1] = 0; out[2] = 1; }
        };
        const float cosCrease = std::cos(40.f * 3.14159265f / 180.f);
        const uint32_t base = uint32_t(b.vertices.size() / 6);
        const size_t idxStart = b.indices.size();
        reserveMore(b.vertices, size_t(nTri) * 18);
        for (int t = 0; t < nTri; ++t) {
            float ut[3];
            unit(t, ut);
            for (int k = 0; k < 3; ++k) {
                const int node = tv[size_t(t) * 3 + size_t(k)];
                float sum[3] = {0, 0, 0};
                for (int j = first[size_t(node)]; j < first[size_t(node) + 1]; ++j) {
                    const int t2 = incident[size_t(j)];
                    float u2[3];
                    unit(t2, u2);
                    if (ut[0] * u2[0] + ut[1] * u2[1] + ut[2] * u2[2] >= cosCrease)
                        for (int m = 0; m < 3; ++m) sum[m] += fn[size_t(t2) * 3 + size_t(m)];
                }
                const float len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                if (len > 0) { sum[0] /= len; sum[1] /= len; sum[2] /= len; }
                else { sum[0] = ut[0]; sum[1] = ut[1]; sum[2] = ut[2]; }
                b.vertices.insert(b.vertices.end(), {pos[size_t(node) * 3], pos[size_t(node) * 3 + 1],
                                                     pos[size_t(node) * 3 + 2], sum[0], sum[1], sum[2]});
                b.indices.push_back(base + uint32_t(t) * 3 + uint32_t(k));
            }
        }
        addPartRange(b, idxStart);
        model.triangleCount += size_t(nTri);
        ++model.faceCount;
    }

    void addFace(const TopoDS_Face& face, const TopLoc_Location& instLoc, const Color& color) {
        TopLoc_Location faceLoc;
        handle<Poly_Triangulation> tri = BRep_Tool::Triangulation(face, faceLoc);
        if (tri.IsNull() || tri->NbTriangles() == 0) return;

        const gp_Trsf trsf = (instLoc * faceLoc).Transformation();
        const bool flip = (face.Orientation() == TopAbs_REVERSED) != trsf.IsNegative();

        MeshBatch& b = batchFor(color);
        const uint32_t base = uint32_t(b.vertices.size() / 6);
        const int nNodes = tri->NbNodes();

        std::vector<float> pos(size_t(nNodes) * 3);
        for (int i = 1; i <= nNodes; ++i) {
            gp_Pnt p = tri->Node(i).Transformed(trsf);
            float* o = &pos[size_t(i - 1) * 3];
            o[0] = float(p.X());
            o[1] = float(p.Y());
            o[2] = float(p.Z());
            grow(o[0], o[1], o[2]);
        }

        if (BRep_Tool::Surface(face).IsNull()) {
            addMeshOnlyFace(b, pos, tri);
            return;
        }

        std::vector<float> nrm(size_t(nNodes) * 3, 0.f);
        const size_t idxStart = b.indices.size();
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
            int a, c, d;
            tri->Triangle(t).Get(a, c, d);
            if (flip) std::swap(c, d);
            --a; --c; --d;
            const float* pa = &pos[size_t(a) * 3];
            const float* pc = &pos[size_t(c) * 3];
            const float* pd = &pos[size_t(d) * 3];
            float u[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
            float v[3] = {pd[0] - pa[0], pd[1] - pa[1], pd[2] - pa[2]};
            float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
            for (int k = 0; k < 3; ++k) {
                nrm[size_t(a) * 3 + k] += n[k];
                nrm[size_t(c) * 3 + k] += n[k];
                nrm[size_t(d) * 3 + k] += n[k];
            }
            b.indices.push_back(base + uint32_t(a));
            b.indices.push_back(base + uint32_t(c));
            b.indices.push_back(base + uint32_t(d));
        }
        addPartRange(b, idxStart);

        reserveMore(b.vertices, size_t(nNodes) * 6);
        for (int i = 0; i < nNodes; ++i) {
            float* n = &nrm[size_t(i) * 3];
            float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0) { n[0] /= len; n[1] /= len; n[2] /= len; }
            else { n[0] = 0; n[1] = 0; n[2] = 1; }
            b.vertices.insert(b.vertices.end(), {pos[size_t(i) * 3], pos[size_t(i) * 3 + 1],
                                                 pos[size_t(i) * 3 + 2], n[0], n[1], n[2]});
        }
        model.triangleCount += size_t(tri->NbTriangles());
        ++model.faceCount;
    }

    void addEdges(const TopoDS_Shape& shape, const TopLoc_Location& instLoc) {
        TopTools_IndexedMapOfShape seen;
        for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
            const TopoDS_Face& face = TopoDS::Face(fe.Current());
            TopLoc_Location faceLoc;
            handle<Poly_Triangulation> tri = BRep_Tool::Triangulation(face, faceLoc);
            if (tri.IsNull()) continue;
            const gp_Trsf trsf = (instLoc * faceLoc).Transformation();
            for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                const TopoDS_Edge& edge = TopoDS::Edge(ee.Current());
                // Seams (e.g. on spheres and cylinders) are not real feature edges
                if (BRep_Tool::Degenerated(edge) || BRep_Tool::IsClosed(edge, face)) continue;
                if (seen.Add(edge) == 0) continue;
                TopLoc_Location eLoc;
                handle<Poly_PolygonOnTriangulation> poly =
                    BRep_Tool::PolygonOnTriangulation(edge, tri, eLoc);
                if (poly.IsNull()) continue;
                for (int i = 1; i < poly->NbNodes(); ++i) {
                    gp_Pnt p0 = tri->Node(poly->Node(i)).Transformed(trsf);
                    gp_Pnt p1 = tri->Node(poly->Node(i + 1)).Transformed(trsf);
                    model.edges.insert(model.edges.end(),
                                       {float(p0.X()), float(p0.Y()), float(p0.Z()),
                                        float(p1.X()), float(p1.Y()), float(p1.Z())});
                }
            }
        }
    }

    // Edges that belong to no face (wireframe data such as DXF lines) are drawn as polylines.
    void addFreeEdges(const TopoDS_Shape& shape, const TopLoc_Location& instLoc) {
        const gp_Trsf trsf = instLoc.Transformation();
        for (TopExp_Explorer ee(shape, TopAbs_EDGE, TopAbs_FACE); ee.More(); ee.Next()) {
            const TopoDS_Edge& edge = TopoDS::Edge(ee.Current());
            if (BRep_Tool::Degenerated(edge) || !BRep_Tool::IsGeometric(edge)) continue;
            BRepAdaptor_Curve curve(edge);
            const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
            if (Precision::IsInfinite(t0) || Precision::IsInfinite(t1)) continue;
            const int steps = curve.GetType() == GeomAbs_Line ? 1 : 48;
            gp_Pnt prev = curve.Value(t0).Transformed(trsf);
            grow(float(prev.X()), float(prev.Y()), float(prev.Z()));
            for (int i = 1; i <= steps; ++i) {
                const gp_Pnt p = curve.Value(t0 + (t1 - t0) * i / steps).Transformed(trsf);
                grow(float(p.X()), float(p.Y()), float(p.Z()));
                model.edges.insert(model.edges.end(), {float(prev.X()), float(prev.Y()), float(prev.Z()),
                                                       float(p.X()), float(p.Y()), float(p.Z())});
                prev = p;
            }
        }
    }

    static std::string labelName(const TDF_Label& l) {
        handle<TDataStd_Name> n;
        if (l.FindAttribute(TDataStd_Name::GetID(), n) && !n.IsNull()) {
            const TCollection_AsciiString a(n->Get());  // UTF-8
            return a.ToCString();
        }
        return {};
    }

    void walk(const TDF_Label& label, const TopLoc_Location& loc, Color inherited, std::string nameHint = {},
              int parentNode = -1) {
        Color own = inherited;
        labelColor(label, own);

        if (shapes->IsAssembly(label)) {
            std::string asmName = !nameHint.empty() ? nameHint : labelName(label);
            if (asmName.empty()) asmName = tr8("Assembly");
            model.nodes.push_back({asmName, parentNode, -1});
            const int thisNode = int(model.nodes.size()) - 1;
            TDF_LabelSequence comps;
            shapes->GetComponents(label, comps);
            for (int i = 1; i <= comps.Length(); ++i) {
                TDF_Label comp = comps.Value(i);
                TDF_Label ref;
                if (!shapes->GetReferredShape(comp, ref)) ref = comp;
                Color c = own;
                labelColor(comp, c);
                std::string nm = labelName(ref);
                if (nm.empty()) nm = labelName(comp);
                walk(ref, loc * XCAFDoc_ShapeTool::GetLocation(comp), c, nm, thisNode);
            }
            return;
        }

        TopoDS_Shape shape;
        const auto repaired = isPartLabel(label) ? healed.find(label.Tag()) : healed.end();
        shape = repaired != healed.end() ? repaired->second : shapes->GetShape(label);
        if (shape.IsNull()) return;

        if (splitFaceCompounds && shape.ShapeType() == TopAbs_COMPOUND) {
            std::vector<TopoDS_Face> faces;
            bool onlyFaces = true;
            for (TopoDS_Iterator it(shape, true, false); it.More(); it.Next()) {
                if (it.Value().ShapeType() != TopAbs_FACE) { onlyFaces = false; break; }
                faces.push_back(TopoDS::Face(it.Value()));
            }
            if (onlyFaces && faces.size() > 1) {
                std::string groupName = !nameHint.empty() ? nameHint : labelName(label);
                if (groupName.empty()) groupName = tr8("Scene");
                model.nodes.push_back({groupName, parentNode, -1});
                const int groupNode = int(model.nodes.size()) - 1;
                for (const TopoDS_Face& face : faces) {
                    Part p;
                    p.name = tr8f("Shape %1", {std::to_string(model.parts.size() + 1)});
                    model.nodes.push_back({p.name, groupNode, int(model.parts.size())});
                    model.parts.push_back(std::move(p));
                    Color c = own;
                    shapeColor(face, c);
                    addFace(face, loc, c);
                }
                return;
            }
        }

        Part part;
        part.name = !nameHint.empty() ? nameHint : labelName(label);
        if (part.name.empty()) part.name = tr8f("Part %1", {std::to_string(model.parts.size() + 1)});
        if (partMeta && label.Father() == shapes->Label()) {
            const auto meta = partMeta->find(label.Tag());
            if (meta != partMeta->end()) {
                part.object = meta->second.object;
                part.filaments = meta->second.filaments;
            }
        }
        model.nodes.push_back({part.name, parentNode, int(model.parts.size())});
        model.parts.push_back(std::move(part));
        for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
            const TopoDS_Face& face = TopoDS::Face(fe.Current());
            Color c = own;
            faceColor(label, shape, face, c);
            addFace(face, loc, c);
        }
        const size_t edgeStart = model.edges.size() / 3;
        addEdges(shape, loc);
        addFreeEdges(shape, loc);
        model.parts.back().edgeFirst = uint32_t(edgeStart);
        model.parts.back().edgeCount = uint32_t(model.edges.size() / 3 - edgeStart);
    }
};

}  // namespace

namespace {

enum class Format { Step, Iges, Brep, Stl, Obj, Ply, Vrml, Gltf, ThreeMf, Dxf };

const char* formatName(Format f) {
    switch (f) {
    case Format::Step: return "STEP";
    case Format::Iges: return "IGES";
    case Format::Brep: return "BREP";
    case Format::Stl: return "STL";
    case Format::Obj: return "OBJ";
    case Format::Ply: return "PLY";
    case Format::Vrml: return "VRML";
    case Format::Gltf: return "glTF";
    case Format::ThreeMf: return "3MF";
    case Format::Dxf: return "DXF";
    }
    return "";
}

// These formats store a single shape or mesh, so there is never an assembly tree to show.
bool isSingleObjectFormat(Format f) { return f == Format::Stl || f == Format::Ply || f == Format::Brep; }

std::string lowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t sep = path.find_last_of("/\\");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return {};
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

Format formatOf(const std::string& path) {
    const std::string e = lowerExtension(path);
    if (e == "stp" || e == "step") return Format::Step;
    if (e == "igs" || e == "iges") return Format::Iges;
    if (e == "brep") return Format::Brep;  // same list as src/Formats.h, the Store manifest and the right-click entry
    if (e == "stl") return Format::Stl;
    if (e == "obj") return Format::Obj;
    if (e == "ply") return Format::Ply;
    if (e == "wrl" || e == "vrml") return Format::Vrml;
    if (e == "gltf" || e == "glb") return Format::Gltf;
    if (e == "3mf") return Format::ThreeMf;
    if (e == "dxf") return Format::Dxf;
    if (e == "sldprt" || e == "sldasm")
        throw std::runtime_error(
            tr8("SolidWorks files (.sldprt / .sldasm) can't be opened directly, because SolidWorks keeps the format "
                "closed.\n\nIn SolidWorks, use File > Save As > STEP (*.step), then open that STEP file here."));
    throw std::runtime_error(tr8f("Unsupported file type%1. Supported: STEP, IGES, BREP, STL, OBJ, PLY, VRML, glTF/GLB, 3MF and DXF.",
                                  {e.empty() ? std::string() : " \"." + e + "\""}));
}

std::string baseName(const std::string& path) {
    const size_t sep = path.find_last_of("/\\");
    std::string n = sep == std::string::npos ? path : path.substr(sep + 1);
    const size_t dot = n.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}

void setLabelName(const TDF_Label& label, const std::string& path) {
    TDataStd_Name::Set(label, TCollection_ExtendedString(baseName(path).c_str(), true));
}

// Wraps a triangulation in a face so the mesh formats go through the same builder as B-rep models.
void addMeshShape(const handle<XCAFDoc_ShapeTool>& shapes, const handle<Poly_Triangulation>& tri,
                  const std::string& path) {
    if (tri.IsNull() || tri->NbTriangles() == 0) throw std::runtime_error(tr8("The file contains no triangles."));
    BRep_Builder bb;
    TopoDS_Face face;
    bb.MakeFace(face, tri);
    setLabelName(shapes->AddShape(face, false), path);
}

// Adds the meshes (with their colours) and line work read by ExtraFormats.cpp as free shapes of the document.
// The big point/triangle arrays of `scene` are released once copied, so large files do not hold two copies in memory.
// When the scene has a tree (e.g. plate -> object -> part) it becomes nested assemblies.
std::map<int, PartMeta> addScene(const handle<XCAFDoc_ShapeTool>& shapes, const handle<XCAFDoc_ColorTool>& colors,
                                 Scene& scene) {
    std::map<int, PartMeta> partMeta;
    std::vector<TDF_Label> meshLabels(scene.meshes.size());
    for (size_t mi = 0; mi < scene.meshes.size(); ++mi) {
        SceneMesh& m = scene.meshes[mi];
        if (m.triangles.empty()) continue;
        handle<Poly_Triangulation> tri = new Poly_Triangulation(int(m.points.size()), int(m.triangles.size()), false);
        for (size_t i = 0; i < m.points.size(); ++i)
            tri->SetNode(int(i) + 1, gp_Pnt(m.points[i][0], m.points[i][1], m.points[i][2]));
        for (size_t i = 0; i < m.triangles.size(); ++i)
            tri->SetTriangle(int(i) + 1, Poly_Triangle(m.triangles[i][0] + 1, m.triangles[i][1] + 1, m.triangles[i][2] + 1));
        BRep_Builder bb;
        TopoDS_Face face;
        bb.MakeFace(face, tri);
        const TDF_Label label = shapes->AddShape(face, false);
        meshLabels[mi] = label;
        if (!m.objectName.empty() || m.filament > 0) {
            PartMeta pm;
            pm.object = m.objectName;
            if (m.filament > 0) pm.filaments.push_back({m.filament, m.hasColor, m.color, m.filamentType, m.filamentVendor});
            partMeta[label.Tag()] = std::move(pm);
        }
        TDataStd_Name::Set(label, TCollection_ExtendedString(m.name.c_str(), true));
        if (m.hasColor)
            colors->SetColor(label, Quantity_Color(m.color[0], m.color[1], m.color[2], Quantity_TOC_sRGB),
                             XCAFDoc_ColorSurf);
        std::vector<Vec3>().swap(m.points);
        std::vector<std::array<int, 3>>().swap(m.triangles);
    }
    if (!scene.tree.empty()) {
        std::function<TDF_Label(const SceneNode&)> build = [&](const SceneNode& n) -> TDF_Label {
            if (n.mesh >= 0) return size_t(n.mesh) < meshLabels.size() ? meshLabels[size_t(n.mesh)] : TDF_Label();
            const TDF_Label group = shapes->NewShape();
            TDataStd_Name::Set(group, TCollection_ExtendedString(n.name.c_str(), true));
            int added = 0;
            for (const SceneNode& child : n.children) {
                const TDF_Label c = build(child);
                if (c.IsNull()) continue;
                shapes->AddComponent(group, c, TopLoc_Location());
                ++added;
            }
            return added ? group : TDF_Label();
        };
        for (const SceneNode& top : scene.tree) build(top);
        shapes->UpdateAssemblies();
    }
    for (const SceneLines& l : scene.lines) {
        BRep_Builder bb;
        TopoDS_Compound compound;
        bb.MakeCompound(compound);
        int count = 0;
        for (const auto& s : l.segments) {
            const gp_Pnt a(s.first[0], s.first[1], s.first[2]), b(s.second[0], s.second[1], s.second[2]);
            if (a.Distance(b) < 1e-9) continue;
            BRepBuilderAPI_MakeEdge edge(a, b);
            if (!edge.IsDone()) continue;
            bb.Add(compound, edge.Edge());
            ++count;
        }
        if (count == 0) continue;
        const TDF_Label label = shapes->AddShape(compound, false);
        TDataStd_Name::Set(label, TCollection_ExtendedString(l.name.c_str(), true));
    }
    return partMeta;
}

// ---- PLY (ASCII and binary; OpenCASCADE can only write it) ----

struct PlyProp {
    std::string name, type, countType;
    bool list = false;
};

struct PlyElement {
    std::string name;
    size_t count = 0;
    std::vector<PlyProp> props;
};

int plyTypeSize(const std::string& t) {
    if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
    if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
    if (t == "int" || t == "uint" || t == "float" || t == "int32" || t == "uint32" || t == "float32") return 4;
    if (t == "double" || t == "float64") return 8;
    return 0;
}

double plyDecode(const char* src, const std::string& t, bool swap) {
    char b[8];
    const int n = plyTypeSize(t);
    std::memcpy(b, src, size_t(n));
    if (swap) std::reverse(b, b + n);
    if (t == "char" || t == "int8") { int8_t v; std::memcpy(&v, b, 1); return v; }
    if (t == "uchar" || t == "uint8") { uint8_t v; std::memcpy(&v, b, 1); return v; }
    if (t == "short" || t == "int16") { int16_t v; std::memcpy(&v, b, 2); return v; }
    if (t == "ushort" || t == "uint16") { uint16_t v; std::memcpy(&v, b, 2); return v; }
    if (t == "int" || t == "int32") { int32_t v; std::memcpy(&v, b, 4); return v; }
    if (t == "uint" || t == "uint32") { uint32_t v; std::memcpy(&v, b, 4); return v; }
    if (t == "float" || t == "float32") { float v; std::memcpy(&v, b, 4); return v; }
    double v;
    std::memcpy(&v, b, 8);
    return v;
}

handle<Poly_Triangulation> readPly(const std::string& path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) throw std::runtime_error(tr8f("Could not read %1 file.", {"PLY"}));

    std::string line;
    std::getline(in, line);
    if (line.rfind("ply", 0) != 0) throw std::runtime_error(tr8f("Not a valid %1 file.", {"PLY"}));

    enum { Ascii, BinLE, BinBE } mode = Ascii;
    std::vector<PlyElement> elements;
    bool ended = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "format") {
            std::string f;
            ls >> f;
            mode = f == "ascii" ? Ascii : (f == "binary_little_endian" ? BinLE : BinBE);
        } else if (key == "element") {
            PlyElement e;
            ls >> e.name >> e.count;
            elements.push_back(e);
        } else if (key == "property" && !elements.empty()) {
            PlyProp p;
            std::string t;
            ls >> t;
            if (t == "list") {
                p.list = true;
                ls >> p.countType >> p.type >> p.name;
            } else {
                p.type = t;
                ls >> p.name;
            }
            elements.back().props.push_back(p);
        } else if (key == "end_header") {
            ended = true;
            break;
        }
    }
    if (!ended) throw std::runtime_error(tr8f("Not a valid %1 file.", {"PLY"}));

    const bool swap = mode == BinBE;  // the host is little-endian
    auto next = [&](const std::string& type) -> double {
        if (mode == Ascii) {
            double v = 0;
            if (!(in >> v)) throw std::runtime_error(tr8("Unexpected end of PLY file."));
            return v;
        }
        const int n = plyTypeSize(type);
        if (n == 0) throw std::runtime_error(tr8f("Unsupported PLY property type: %1", {type}));
        char buf[8];
        if (!in.read(buf, n)) throw std::runtime_error(tr8("Unexpected end of PLY file."));
        return plyDecode(buf, type, swap);
    };

    std::vector<gp_Pnt> points;
    std::vector<std::array<int, 3>> tris;
    for (const PlyElement& e : elements) {
        const bool isVertex = e.name == "vertex", isFace = e.name == "face";
        for (size_t i = 0; i < e.count; ++i) {
            double xyz[3] = {0, 0, 0};
            for (const PlyProp& p : e.props) {
                if (p.list) {
                    const int n = int(next(p.countType));
                    std::vector<int> idx;
                    for (int k = 0; k < n; ++k) idx.push_back(int(next(p.type)));
                    if (isFace && (p.name == "vertex_indices" || p.name == "vertex_index"))
                        for (int k = 1; k + 1 < n; ++k) tris.push_back({idx[0], idx[size_t(k)], idx[size_t(k) + 1]});
                } else {
                    const double v = next(p.type);
                    if (isVertex) {
                        if (p.name == "x") xyz[0] = v;
                        else if (p.name == "y") xyz[1] = v;
                        else if (p.name == "z") xyz[2] = v;
                    }
                }
            }
            if (isVertex) points.emplace_back(xyz[0], xyz[1], xyz[2]);
        }
    }

    const int nv = int(points.size());
    handle<Poly_Triangulation> tri = new Poly_Triangulation(nv, int(tris.size()), false);
    for (int i = 0; i < nv; ++i) tri->SetNode(i + 1, points[size_t(i)]);
    for (size_t i = 0; i < tris.size(); ++i) {
        for (int v : tris[i])
            if (v < 0 || v >= nv) throw std::runtime_error(tr8("PLY file has an invalid vertex index."));
        tri->SetTriangle(int(i) + 1, Poly_Triangle(tris[i][0] + 1, tris[i][1] + 1, tris[i][2] + 1));
    }
    return tri;
}

}  // namespace

std::shared_ptr<Model> loadModelFile(const std::string& path, MeshQuality quality, LoadProgress* progress) {
    setStage(progress, LoadStage::Reading, 0.f);
    const auto t0 = std::chrono::steady_clock::now();
    // Set the environment variable CADSCOUT_TIMING to print how long each loading stage takes (for speed tests).
    const bool timing = std::getenv("CADSCOUT_TIMING") != nullptr;
    auto lap = [&, last = t0](const char* stage) mutable {
        const auto now = std::chrono::steady_clock::now();
        if (timing) std::fprintf(stderr, "[timing] %-22s %.3f s\n", stage, std::chrono::duration<double>(now - last).count());
        last = now;
    };
    const Format format = formatOf(path);

    handle<XCAFApp_Application> app = XCAFApp_Application::GetApplication();
    handle<TDocStd_Document> doc;
    app->NewDocument("BinXCAF", doc);
    // Close the document on every exit path, including the error throws below.
    struct DocCloser {
        handle<XCAFApp_Application>& app;
        handle<TDocStd_Document>& doc;
        ~DocCloser() { app->Close(doc); }
    } closer{app, doc};

    const handle<XCAFDoc_ShapeTool> shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    bool needsMeshing = true;  // B-rep formats are tessellated here; mesh formats already are triangles
    bool b_splitFaceCompounds = false;
    bool parallelRepair = false;  // STEP: repair the parts on all cores after translating
    std::string sceneInfo;  // short summary from the 3MF/DXF readers
    std::vector<InfoSection> sceneSections;  // the file's own information (3MF projects)
    std::vector<ScenePlate> scenePlates;
    std::map<int, PartMeta> partMeta;

    switch (format) {
    case Format::Step: {
        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(false);
        reader.SetPropsMode(false);
        reader.SetMatMode(false);
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) throw std::runtime_error(tr8f("Could not read %1 file.", {"STEP"}));
        checkCancel(progress);
        lap("STEP parse");
        setStage(progress, LoadStage::Translating, 0.15f);
        // OpenCASCADE's built-in shape repair is switched off here; the same repair is done afterwards on all cores
        // (Builder::healParts).
        parallelRepair = true;
        reader.ChangeReader().SetShapeProcessFlags(ShapeProcess::OperationsFlags());
        {
            const handle<LoadIndicator> indicator = new LoadIndicator(progress, 0.15f, 0.30f);
            const bool ok = reader.Transfer(doc, indicator->Start());
            checkCancel(progress);
            if (!ok) throw std::runtime_error(tr8f("Could not translate %1 file.", {"STEP"}));
        }
        lap("STEP translate");
        break;
    }
    case Format::Iges: {
        IGESCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(false);
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) throw std::runtime_error(tr8f("Could not read %1 file.", {"IGES"}));
        if (!reader.Transfer(doc)) throw std::runtime_error(tr8f("Could not translate %1 file.", {"IGES"}));
        break;
    }
    case Format::Brep: {
        TopoDS_Shape shape;
        BRep_Builder builder;
        if (!BRepTools::Read(shape, path.c_str(), builder) || shape.IsNull())
            throw std::runtime_error(tr8f("Could not read %1 file.", {"BREP"}));
        setLabelName(shapeTool->AddShape(shape, false), path);
        break;
    }
    case Format::Stl:
        needsMeshing = false;
        // Equal nodes are only merged between triangles within ~34 degrees, so sharp edges stay sharp.
        addMeshShape(shapeTool, RWStl::ReadFile(path.c_str(), 0.6), path);
        break;
    case Format::Ply:
        needsMeshing = false;
        addMeshShape(shapeTool, readPly(path), path);
        break;
    case Format::Obj: {
        needsMeshing = false;
        RWObj_CafReader reader;
        reader.SetDocument(doc);
        if (!reader.Perform(TCollection_AsciiString(path.c_str()), Message_ProgressRange()))
            throw std::runtime_error(tr8f("Could not read %1 file.", {"OBJ"}));
        break;
    }
    case Format::Vrml: {
        needsMeshing = false;
        VrmlAPI_CafReader reader;
        reader.SetDocument(doc);
        reader.SetFileLengthUnit(1.0);  // unset it is -1, which mirrors the whole model through the origin
        b_splitFaceCompounds = true;    // each VRML Shape arrives as one face: show each as its own part
        if (!reader.Perform(TCollection_AsciiString(path.c_str()), Message_ProgressRange()))
            throw std::runtime_error(tr8f("Could not read %1 file.", {"VRML"}));
        break;
    }
    case Format::Gltf: {
        needsMeshing = false;
        RWGltf_CafReader reader;
        reader.SetDocument(doc);
        reader.SetSystemCoordinateSystem(RWMesh_CoordinateSystem_Zup);  // glTF is Y-up; the viewer is Z-up
        if (!reader.Perform(TCollection_AsciiString(path.c_str()), Message_ProgressRange()))
            throw std::runtime_error(tr8f("Could not read %1 file.", {"glTF/GLB"}));
        break;
    }
    case Format::ThreeMf: {
        needsMeshing = false;
        Scene scene = read3mf(path);
        sceneInfo = scene.info;
        scenePlates = scene.plates;
        sceneSections = std::move(scene.sections);
        partMeta = addScene(shapeTool, XCAFDoc_DocumentTool::ColorTool(doc->Main()), scene);
        break;
    }
    case Format::Dxf: {
        needsMeshing = false;
        Scene scene = readDxf(path);
        sceneInfo = scene.info;
        addScene(shapeTool, XCAFDoc_DocumentTool::ColorTool(doc->Main()), scene);
        break;
    }
    }

    auto model = std::make_shared<Model>();
    model->formatName = formatName(format);
    model->singleObject = isSingleObjectFormat(format);
    model->extraInfo = sceneInfo;
    model->sections = std::move(sceneSections);
    for (const ScenePlate& sp : scenePlates) {
        PlateShape plate;
        for (const auto& p : sp.outline) plate.outline.push_back({float(p[0]), float(p[1])});
        plate.name = sp.name;
        plate.printSeconds = sp.printSeconds;
        model->plates.push_back(std::move(plate));
    }
    Builder b(*model);
    TDF_Label main = doc->Main();
    b.shapes = shapeTool;
    b.splitFaceCompounds = b_splitFaceCompounds;
    b.partMeta = &partMeta;
    b.colors = XCAFDoc_DocumentTool::ColorTool(main);
    b.materials = XCAFDoc_DocumentTool::VisMaterialTool(main);

    TDF_LabelSequence freeShapes;
    b.shapes->GetFreeShapes(freeShapes);
    if (freeShapes.IsEmpty()) throw std::runtime_error(tr8("The file contains no shapes."));
    // Instanced meshes (glTF nodes sharing one mesh): a node placed at the origin becomes a plain "original" label and
    // later nodes become top-level references to it. Only the references count as free shapes, so the original would be
    // lost. A located node instead gets a hidden template label created right after its reference (next tag number);
    // those templates must stay hidden. So add each original that is not such a template.
    {
        const int freeCount = freeShapes.Length();
        for (int i = 1; i <= freeCount; ++i) {
            TDF_Label original;
            if (!b.shapes->IsReference(freeShapes.Value(i)) ||
                !XCAFDoc_ShapeTool::GetReferredShape(freeShapes.Value(i), original) || original.IsNull() ||
                XCAFDoc_ShapeTool::IsFree(original) || original.Father() != b.shapes->Label())
                continue;
            const TDF_Label before = b.shapes->Label().FindChild(original.Tag() - 1, false);
            TDF_Label beforeTarget;
            if (original.Tag() > 1 && !before.IsNull() && XCAFDoc_ShapeTool::GetReferredShape(before, beforeTarget) &&
                beforeTarget.IsEqual(original))
                continue;  // hidden template of the instance just before it
            TDF_LabelSequence users;
            XCAFDoc_ShapeTool::GetUsers(original, users);
            bool onlyTopLevel = true;
            for (int u = 1; u <= users.Length(); ++u)
                if (!XCAFDoc_ShapeTool::IsFree(users.Value(u))) onlyTopLevel = false;
            bool known = false;
            for (int k = 1; k <= freeShapes.Length(); ++k)
                if (freeShapes.Value(k).IsEqual(original)) known = true;
            if (onlyTopLevel && !known) freeShapes.Append(original);
        }
    }

    // Use the triangulation when a shape has one (mesh formats have no surfaces to measure).
    checkCancel(progress);
    Bnd_Box box;
    if (parallelRepair) {
        setStage(progress, LoadStage::Repairing, 0.30f);
        b.progress = progress;
        box = b.healParts(freeShapes);
        lap("STEP repair");
    } else {
        for (int i = 1; i <= freeShapes.Length(); ++i)
            BRepBndLib::Add(b.shapes->GetShape(freeShapes.Value(i)), box, true);
    }
    if (box.IsVoid()) throw std::runtime_error(tr8("The file contains no geometry."));
    const double diag = std::sqrt(box.SquareExtent());
    lap("tree + bounds");

    if (needsMeshing) {
        // Linear deflection (fraction of the bounding-box diagonal) and angular deflection (radians)
        static const double kLinear[] = {4e-3, 1e-3, 2e-4};
        static const double kAngular[] = {0.8, 0.5, 0.2};
        const int q = std::clamp(int(quality), 0, 2);
        IMeshTools_Parameters params;  // the same values the short BRepMesh_IncrementalMesh constructor sets
        params.Deflection = std::fmax(diag * kLinear[q], 1e-6);
        params.Angle = kAngular[q];
        params.Relative = false;
        params.InParallel = true;
        const float meshFrom = parallelRepair ? 0.70f : 0.20f;
        setStage(progress, LoadStage::Meshing, meshFrom);
        const handle<LoadIndicator> indicator = new LoadIndicator(progress, meshFrom, 0.95f);
        if (parallelRepair) {
            BRepMesh_IncrementalMesh mesher(b.healedCompound(), params, indicator->Start());
        } else {
            Message_ProgressScope scope(indicator->Start(), "Meshing", freeShapes.Length());
            for (int i = 1; i <= freeShapes.Length() && scope.More(); ++i)
                BRepMesh_IncrementalMesh mesher(b.shapes->GetShape(freeShapes.Value(i)), params, scope.Next());
        }
        checkCancel(progress);
        lap("meshing");
    }

    setStage(progress, LoadStage::Preparing, 0.95f);
    for (int i = 1; i <= freeShapes.Length(); ++i) b.walk(freeShapes.Value(i), TopLoc_Location(), Color{});
    checkCancel(progress);
    if (progress) progress->fraction = 1.f;
    lap("display data");
    if (timing)
        std::fprintf(stderr, "[timing] result: %zu parts, %zu faces, %zu triangles, %zu colours\n", model->parts.size(),
                     size_t(model->faceCount), size_t(model->triangleCount), model->batches.size());
    model->loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (model->triangleCount == 0 && model->edges.empty()) throw std::runtime_error(tr8("No displayable surfaces found."));
    return model;
}
