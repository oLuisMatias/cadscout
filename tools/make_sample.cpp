// Generates sample.stp: a small colored assembly used to test the viewer.
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <IGESCAFControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDataStd_Name.hxx>
#include <TopoDS_Compound.hxx>
#include <string>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESData_GlobalSection.hxx>
#include <IGESData_IGESModel.hxx>
#include <TCollection_HAsciiString.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <iostream>

int main(int argc, char** argv) {
    using opencascade::handle;
    handle<XCAFApp_Application> app = XCAFApp_Application::GetApplication();
    handle<TDocStd_Document> doc;
    app->NewDocument("BinXCAF", doc);
    auto st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    auto ct = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    TopoDS_Shape box = BRepPrimAPI_MakeBox(40, 30, 20).Shape();
    gp_Trsf t;
    t.SetTranslation(gp_Vec(20, 15, -5));
    TopoDS_Shape hole = BRepBuilderAPI_Transform(BRepPrimAPI_MakeCylinder(6, 40).Shape(), t).Shape();
    TopoDS_Shape plate = BRepAlgoAPI_Cut(box, hole).Shape();
    TopoDS_Shape ball = BRepPrimAPI_MakeSphere(12).Shape();

    TDF_Label asm_ = st->NewShape();
    auto add = [&](const TopoDS_Shape& s, double r, double g, double b, double dx, double dz) {
        // Both plates share one shape label, so the colour goes on the component (instance) label.
        TDF_Label lab = st->AddShape(s, false);
        gp_Trsf tr;
        tr.SetTranslation(gp_Vec(dx, 0, dz));
        TDF_Label comp = st->AddComponent(asm_, lab, TopLoc_Location(tr));
        ct->SetColor(comp, Quantity_Color(r, g, b, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);
    };
    add(plate, 0.8, 0.2, 0.2, 0, 0);
    add(plate, 0.2, 0.6, 0.9, 60, 0);
    add(ball, 0.2, 0.8, 0.3, 30, 35);
    st->UpdateAssemblies();

    STEPCAFControl_Writer w;
    w.Transfer(doc);
    const char* out = argc > 1 ? argv[1] : "sample.stp";
    if (w.Write(out) != IFSelect_RetDone) { std::cerr << "write failed\n"; return 1; }
    std::cout << "wrote " << out << "\n";

    // The same model in the other formats the viewer opens (IGES, BREP, STL), next to the STEP file.
    const std::string stem = std::string(out).substr(0, std::string(out).find_last_of('.'));
    // IGES gets the three parts as separate top-level shapes (not one assembly), so they stay separate objects.
    handle<TDocStd_Document> flatDoc;
    app->NewDocument("BinXCAF", flatDoc);
    auto flatShapes = XCAFDoc_DocumentTool::ShapeTool(flatDoc->Main());
    auto flatColors = XCAFDoc_DocumentTool::ColorTool(flatDoc->Main());
    auto addFlat = [&](const TopoDS_Shape& s, const char* name, double r, double g, double b, double dx, double dz) {
        gp_Trsf tr;
        tr.SetTranslation(gp_Vec(dx, 0, dz));
        TDF_Label lab = flatShapes->AddShape(BRepBuilderAPI_Transform(s, tr, true).Shape(), false);
        TDataStd_Name::Set(lab, TCollection_ExtendedString(name));
        flatColors->SetColor(lab, Quantity_Color(r, g, b, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);
    };
    addFlat(plate, "Red plate", 0.8, 0.2, 0.2, 0, 0);
    addFlat(plate, "Blue plate", 0.2, 0.6, 0.9, 60, 0);
    addFlat(ball, "Green ball", 0.2, 0.8, 0.3, 30, 35);
    IGESCAFControl_Writer iw;
    iw.Transfer(flatDoc);
    {
        // OpenCASCADE writes the Windows user name as the IGES author; keep personal details out of the sample files.
        IGESData_GlobalSection header = iw.Model()->GlobalSection();
        header.SetAuthorName(new TCollection_HAsciiString("CADscout"));
        header.SetCompanyName(new TCollection_HAsciiString("CADscout"));
        iw.Model()->SetGlobalSection(header);
    }
    if (iw.Write((stem + ".igs").c_str()) != IFSelect_RetDone) { std::cerr << "igs write failed\n"; return 1; }

    BRep_Builder bb;
    TopoDS_Compound all;
    bb.MakeCompound(all);
    auto place = [&](const TopoDS_Shape& sh, double dx, double dz) {
        gp_Trsf tr;
        tr.SetTranslation(gp_Vec(dx, 0, dz));
        bb.Add(all, BRepBuilderAPI_Transform(sh, tr).Shape());
    };
    place(plate, 0, 0);
    place(plate, 60, 0);
    place(ball, 30, 35);
    if (!BRepTools::Write(all, (stem + ".brep").c_str())) { std::cerr << "brep write failed\n"; return 1; }
    BRepMesh_IncrementalMesh(all, 0.2, false, 0.3, true);
    StlAPI_Writer sw;
    if (!sw.Write(all, (stem + ".stl").c_str())) { std::cerr << "stl write failed\n"; return 1; }
    std::cout << "wrote " << stem << ".igs/.brep/.stl\n";
}
