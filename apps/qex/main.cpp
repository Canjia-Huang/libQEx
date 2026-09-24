/*
 * qex - command line application for QEx.
 *
 * Reads a triangle mesh with a (relaxed) integer grid map from an OBJ file,
 * extracts the quad mesh and writes it to another OBJ file. Optionally it also
 * stores the meshes in geogram format, where every piece of information (the
 * correspondence, the quad edge polylines, ...) is kept in mesh attributes.
 *
 * Command line:
 *
 *   qex <input.obj> <output.obj> [-v|--valences <file.vval>] [--geogram <file>]
 *
 * With --geogram the polygonal mesh - the triangle mesh cut along all quad edge
 * polylines, i.e. the quad layout as a subdivision of the triangle mesh - is
 * stored as a GEO::Mesh in the given file.
 *
 * @see QEx::GeogramBridge for the attributes stored in that mesh.
 */

#include <qex.h>
#ifdef QEX_HAVE_GEOGRAM
#include <qex_geogram.h>
#endif

#include <OpenMesh/Core/IO/MeshIO.hh>

#include <CLI/CLI.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace QEx;

namespace {

/** Reads a vertex valence file in the VVAL format expected by the extractor. */
bool readVertexValences(const std::string &fileName, size_t expectedVertexCount,
        std::vector<unsigned int> &out) {
    std::ifstream is(fileName.c_str());
    std::string header;
    static const char *EXPECTED_HEADER = "{\"file_format\":\"VVAL\",\"version\":1}";
    std::getline(is, header);
    if (header != EXPECTED_HEADER) {
        std::cerr << "Error when reading vertex valence file: unexpected file format." << std::endl;
        return false;
    }
    size_t vertexCount = 0;
    is >> vertexCount;
    if (vertexCount != expectedVertexCount) {
        std::cerr << "Error when reading vertex valence file: unexpected number of vertices. "
                  << "Expected " << expectedVertexCount << ", actual " << vertexCount << "."
                  << std::endl;
        return false;
    }
    out.clear();
    out.reserve(vertexCount);
    for (; vertexCount; --vertexCount) {
        int value = 0;
        is >> value;
        out.push_back((unsigned int)value);
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    CLI::App app(
            "qex - extract a quad mesh from a triangle mesh with a relaxed integer grid map\n"
            "Based on the original libQEx command line tool (cmdline_tool): it extracts the quad "
            "mesh exactly as before and additionally exports the polymesh - the quad layout as a "
            "subdivision of the triangle mesh, i.e. the triangle mesh cut along all quad edge "
            "polylines - in geogram format (see --out-poly).");
    /*
     * Print the footer verbatim: CLI11's paragraph formatting would re-wrap it and
     * strip the indentation.
     */
    app.footer(
            "SUPPORT:\n"
            " - Developed by huangcanjia\n"
            " - For bug reports or requirements, please contact: Canjia Huang <huangcanjia0214@gmail.com>");
    if (auto *formatter = dynamic_cast<CLI::Formatter *>(app.get_formatter().get()))
        formatter->enable_footer_formatting(false);

    std::string inputFile;
    std::string outputFile;
    std::string valenceFile;
    std::string polyFile;

    app.add_option("--in,-i", inputFile, "Triangle mesh with face based UVs (OBJ)")
            ->required()
            ->check(CLI::ExistingFile);
    app.add_option("--out,-o", outputFile, "Resulting quad mesh (OBJ)")->required();
    app.add_option("-v,--valences", valenceFile,
            "Vertex valences in VVAL format (optional, helps in ambiguous cases)")
            ->check(CLI::ExistingFile);
#ifdef QEX_HAVE_GEOGRAM
    app.add_option("--out-poly,--o-ploy", polyFile,
            "Store the polygonal mesh (the quad layout as a subdivision of the triangle mesh) "
            "in this file, in geogram's mesh format with all information in mesh attributes");
#else
    app.add_option_function<std::string>("--out-poly,--o-ploy", [](const std::string &) {
            throw CLI::RuntimeError(
                    "QEx was built without geogram support (configure with -DGEOGRAM_ROOT=... or "
                    "-DQEX_WITH_GEOGRAM=ON)", 1);
        }, "Store the polygonal mesh in geogram format (not available in this build)");
#endif

    CLI11_PARSE(app, argc, argv);

    if (!outputFile.empty()) {
        if (std::filesystem::is_directory(outputFile)) {
            std::string model_name;

            const std::filesystem::path inPath(inputFile);
            const std::string filename_w_ex = inPath.filename().string();
            if (const std::size_t pos = filename_w_ex.find_last_of('.');
                pos != std::string::npos)
                model_name = filename_w_ex.substr(0, pos);

            outputFile = outputFile + "/" + model_name + "_quad.obj";
        }
    }
    if (!polyFile.empty()) {
        if (std::filesystem::is_directory(polyFile)) {
            std::string model_name;

            const std::filesystem::path inPath(inputFile);
            const std::string filename_w_ex = inPath.filename().string();
            if (const std::size_t pos = filename_w_ex.find_last_of('.');
                pos != std::string::npos)
                model_name = filename_w_ex.substr(0, pos);

            polyFile = polyFile + "/" + model_name + "_poly.geogram";
        }
    }

    /*
     * ------------------------------------------------------------------
     * Read the input
     * ------------------------------------------------------------------
     */
    TriMesh triMesh;
    triMesh.request_halfedge_texcoords2D();
    OpenMesh::IO::Options readOptions(OpenMesh::IO::Options::FaceTexCoord);
    if (!OpenMesh::IO::read_mesh(triMesh, inputFile, readOptions)) {
        std::cerr << "Could not read " << inputFile << std::endl;
        return 2;
    }
    if (!triMesh.has_halfedge_texcoords2D()) {
        std::cerr << "The input mesh has no per-halfedge texture coordinates." << std::endl;
        return 3;
    }

    std::vector<OpenMesh::Vec2d> uvVector;
    uvVector.reserve(triMesh.n_halfedges());
    for (TriMesh::HalfedgeIter h = triMesh.halfedges_begin(); h != triMesh.halfedges_end(); ++h) {
        const OpenMesh::Vec2f &uv = triMesh.texcoord2D(*h);
        uvVector.push_back(OpenMesh::Vec2d(uv[0], uv[1]));
    }

    std::vector<unsigned int> valences;
    const std::vector<unsigned int> *valencesPtr = 0;
    if (!valenceFile.empty()) {
        if (!readVertexValences(valenceFile, triMesh.n_vertices(), valences)) return 4;
        valencesPtr = &valences;
    }

    /*
     * ------------------------------------------------------------------
     * Extract
     *
     * The layout (which the geogram output is built from) is only computed if it
     * was asked for.
     * ------------------------------------------------------------------
     */
    QuadMesh quadMesh;
    SurfaceLayout layout;
    extractQuadMeshWithLayout(&triMesh, &uvVector, valencesPtr, &quadMesh,
            polyFile.empty() ? 0 : &layout, 0, true);

    if (!outputFile.empty()) {
        if (!OpenMesh::IO::write_mesh(quadMesh, outputFile, OpenMesh::IO::Options::Default, 12)) {
            std::cerr << "Could not write " << outputFile << std::endl;
            return 5;
        }
        std::cout << "wrote " << outputFile << " (" << quadMesh.n_vertices() << " vertices, "
                  << quadMesh.n_faces() << " faces)" << std::endl;
    }

#ifdef QEX_HAVE_GEOGRAM
    if (!polyFile.empty()) {
        if (!GeogramBridge::saveRefinedMesh(layout, polyFile)) {
            std::cerr << "Could not write " << polyFile << std::endl;
            return 5;
        }
        std::cout << "wrote " << polyFile << " (polygonal mesh: the triangle mesh cut along all "
                  << layout.quad_edges.size() << " quad edge polylines, "
                  << layout.face_vertices.size() << " faces, all information in mesh attributes)"
                  << std::endl;
    }
#endif

    return 0;
}
