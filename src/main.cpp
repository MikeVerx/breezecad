#include <BRepPrimAPI_MakeBox.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Shape.hxx>
#include <Interface_Static.hxx>

#include <iostream>

int main() {
    std::cout << "=== OrdoCAD v0.1 ===\n" << std::flush;
    std::cout << "Order from ideas.\n\n" << std::flush;

    // 1. Создаём куб 100x100x100 мм
    std::cout << "Creating a box 100x100x100 mm...\n" << std::flush;
    TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 100.0, 100.0).Shape();

    // 2. Пишем в STEP-файл в папку, где точно есть права на запись
    const char* outPath = "D:/test_ordocad/test.step";

    std::cout << "Writing to " << outPath << "...\n" << std::flush;
    STEPControl_Writer writer;
    Interface_Static::SetCVal("write.step.unit", "MM");
    Interface_Static::SetCVal("write.step.schema", "AP214");

    IFSelect_ReturnStatus status = writer.Transfer(box, STEPControl_AsIs);
    std::cout << "Transfer done, status = " << status << "\n" << std::flush;
    if (status != IFSelect_RetDone) {
        std::cerr << "ERROR: transfer failed!\n" << std::flush;
        return 1;
    }

    std::cout << "Calling writer.Write...\n" << std::flush;
    IFSelect_ReturnStatus wstatus = writer.Write(outPath);
    std::cout << "Write done, status = " << wstatus << "\n" << std::flush;

    if (wstatus != IFSelect_RetDone) {
        std::cerr << "ERROR: write failed!\n" << std::flush;
        return 1;
    }

    std::cout << "SUCCESS! File '" << outPath << "' created.\n" << std::flush;
    std::cout << "Open it in any CAD (FreeCAD, SolidWorks, Kompas) to verify.\n" << std::flush;
    return 0;
}