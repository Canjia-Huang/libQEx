/*
 * Exports the result of the quad extraction into geogram meshes.
 *
 *   <prefix>_refined.geogram   the subdivision of the triangle mesh (cut along
 *                              all quad edge polylines), with the vertex /
 *                              edge / facet / facet-corner attributes listed
 *                              in QEx::GeogramBridge
 *   <prefix>_quad.geogram      the extracted quad mesh, with its cell / quad
 *                              edge attributes
 *
 * Both files are read back afterwards and checked, so that the test also
 * verifies that the attributes survive geogram's IO.
 */

#include <qex.h>
#include <qex_geogram.h>

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_io.h>
#include <geogram/basic/attributes.h>
#include <OpenMesh/Core/IO/MeshIO.hh>

#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace QEx;

namespace {

/* the same names GeogramBridge uses (see qex_geogram.h) */
const char *const ATTR_CELL = "cell";
const char *const ATTR_QUAD_EDGE = "quad_edge";
const char *const ATTR_TRI_FACE = "tri_face";
const char *const ATTR_TRI_FACES_NB = "tri_faces_nb";
const char *const ATTR_QUAD_EDGE_STEP = "quad_edge_step";
const char *const ATTR_BORDER = "border";

int failures = 0;

void check(bool ok, const std::string &what, const std::string &detail = "") {
    std::cout << (ok ? "  [ ok ] " : "  [FAIL] ") << what;
    if (!detail.empty()) std::cout << "   (" << detail << ")";
    std::cout << std::endl;
    if (!ok) ++failures;
}

/** Reads a mesh back (GEO::Mesh is not copyable, so it is passed in). */
bool loadBack(const std::string &path, GEO::Mesh &mesh) {
    if (!GEO::mesh_load(path, mesh)) {
        std::cerr << "Could not read back " << path << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0]
                  << " <infile.obj> <out_prefix> [--valences <file>] [--no-merge]" << std::endl;
        return 1;
    }

    const std::string infile = argv[1];
    const std::string prefix = argv[2];
    std::string valence_file;
    bool merge = true;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--valences") && i + 1 < argc) valence_file = argv[++i];
        else if (!std::strcmp(argv[i], "--no-merge")) merge = false;
    }

    TriMesh triMesh;
    triMesh.request_halfedge_texcoords2D();
    OpenMesh::IO::Options readOpts(OpenMesh::IO::Options::FaceTexCoord);
    if (!OpenMesh::IO::read_mesh(triMesh, infile, readOpts)) {
        std::cerr << "Could not read " << infile << std::endl;
        return 2;
    }

    std::vector<OpenMesh::Vec2d> uvVector;
    uvVector.reserve(triMesh.n_halfedges());
    for (TriMesh::HalfedgeIter h = triMesh.halfedges_begin(); h != triMesh.halfedges_end(); ++h) {
        const OpenMesh::Vec2f &uv = triMesh.texcoord2D(*h);
        uvVector.push_back(OpenMesh::Vec2d(uv[0], uv[1]));
    }

    std::vector<unsigned int> valences;
    const std::vector<unsigned int> *valencesPtr = 0;
    if (!valence_file.empty()) {
        std::ifstream is(valence_file.c_str());
        std::string header;
        std::getline(is, header);
        size_t n = 0;
        is >> n;
        if (n != triMesh.n_vertices()) return 4;
        valences.resize(n);
        for (size_t i = 0; i < n; ++i) is >> valences[i];
        valencesPtr = &valences;
    }

    std::cout << "=== input ===" << std::endl;
    std::cout << "  " << infile << ": " << triMesh.n_vertices() << " vertices, "
              << triMesh.n_faces() << " faces" << std::endl;

    QuadMesh quadMesh;
    SurfaceLayout layout;
    extractQuadMeshWithLayout(&triMesh, &uvVector, valencesPtr, &quadMesh, &layout, 0, merge);

    std::cout << "=== extracted ===" << std::endl;
    std::cout << "  quad mesh: " << quadMesh.n_vertices() << " vertices, " << quadMesh.n_faces()
              << " faces; cells " << layout.n_cells << "; quad edges "
              << layout.quad_edges.size() << "; refined faces "
              << layout.face_vertices.size() << std::endl;

    /*
     * ------------------------------------------------------------------
     * geogram meshes
     * ------------------------------------------------------------------
     */
    GeogramBridge::initialize();

    const std::string refined_path = prefix + "_refined.geogram";
    const std::string quad_path = prefix + "_quad.geogram";

    GEO::Mesh refined;
    GeogramBridge::toRefinedMesh(layout, refined);
    std::cout << "=== refined mesh (subdivision of the triangle mesh) ===" << std::endl;
    std::cout << GeogramBridge::describe(refined);
    check(GEO::mesh_save(refined, refined_path), "refined mesh written to " + refined_path);

    GEO::Mesh quads;
    GeogramBridge::toQuadMesh(layout, quadMesh, quads);
    std::cout << "=== quad mesh ===" << std::endl;
    std::cout << GeogramBridge::describe(quads);
    check(GEO::mesh_save(quads, quad_path), "quad mesh written to " + quad_path);

    /*
     * ------------------------------------------------------------------
     * read back and verify that everything survived
     * ------------------------------------------------------------------
     */
    std::cout << "=== verification (after reading the files back) ===" << std::endl;

    GEO::Mesh refined_in;
    check(loadBack(refined_path, refined_in), "refined mesh can be read back");
    check(refined_in.vertices.nb() == (GEO::index_t)layout.vertex_position.size(),
            "refined mesh vertices survived",
            std::to_string(refined_in.vertices.nb()) + " of "
                    + std::to_string(layout.vertex_position.size()));
    check(refined_in.facets.nb() == (GEO::index_t)layout.face_vertices.size(),
            "refined mesh faces survived",
            std::to_string(refined_in.facets.nb()) + " of "
                    + std::to_string(layout.face_vertices.size()));

    {
        /* cell and tri_face are unsigned int, quad_face and tri_faces_nb differ */
        const bool have_cell = GEO::Attribute<unsigned int>::is_defined(
                refined_in.facets.attributes(), ATTR_CELL);
        const bool have_tri = GEO::Attribute<unsigned int>::is_defined(
                refined_in.facets.attributes(), ATTR_TRI_FACE);
        const bool have_quad_face = GEO::Attribute<int>::is_defined(
                refined_in.facets.attributes(), "quad_face");
        const bool have_tri_nb = GEO::Attribute<unsigned int>::is_defined(
                refined_in.facets.attributes(), ATTR_TRI_FACES_NB);
        check(have_cell && have_tri && have_quad_face && have_tri_nb,
                "facet attributes cell / tri_face / quad_face / tri_faces_nb are present");
        if (have_cell && have_tri && have_quad_face && have_tri_nb) {
            const GEO::Attribute<unsigned int> cell(refined_in.facets.attributes(), ATTR_CELL);
            const GEO::Attribute<unsigned int> tri(refined_in.facets.attributes(), ATTR_TRI_FACE);
            const GEO::Attribute<int> quad_face(refined_in.facets.attributes(), "quad_face");
            const GEO::Attribute<unsigned int> tri_nb(refined_in.facets.attributes(),
                    ATTR_TRI_FACES_NB);
            size_t matching = 0, cells_ok = 0, holes = 0;
            std::map<unsigned int, unsigned int> nb_of_cell;
            std::map<unsigned int, int> quad_face_of_cell;
            for (GEO::index_t f = 0; f < refined_in.facets.nb(); ++f) {
                if ((int)cell[f] == layout.face_cell[f] && (int)tri[f] == layout.face_tri_face[f])
                    ++matching;
                const unsigned int c = cell[f];
                if (!nb_of_cell.count(c)) { nb_of_cell[c] = tri_nb[f]; quad_face_of_cell[c] = quad_face[f]; }
                else if (nb_of_cell[c] == tri_nb[f] && quad_face_of_cell[c] == quad_face[f]) ++cells_ok;
                if (quad_face[f] < 0) ++holes;
            }
            check(matching == (size_t)refined_in.facets.nb(),
                    "cell and triangle index of every refined face round-trip",
                    std::to_string(matching) + " of " + std::to_string(refined_in.facets.nb()));
            check(cells_ok == (size_t)refined_in.facets.nb() - nb_of_cell.size(),
                    "tri_faces_nb and quad_face are constant within a cell",
                    std::to_string(nb_of_cell.size()) + " cells");
            check(holes > 0 || layout.n_cells_without_poly_face == 0,
                    "cells without a quad face are marked with quad_face == -1",
                    std::to_string(holes) + " facets in "
                            + std::to_string(layout.n_cells_without_poly_face)
                            + " hole cell(s)");
        }
    }

    {
        const bool have_step = GEO::Attribute<int>::is_defined(
                refined_in.vertices.attributes(), ATTR_QUAD_EDGE_STEP);
        const bool have_qe = GEO::Attribute<int>::is_defined(
                refined_in.vertices.attributes(), ATTR_QUAD_EDGE);
        check(have_step && have_qe,
                "vertex attributes quad_edge / quad_edge_step are present");
        if (have_step && have_qe) {
            const GEO::Attribute<int> step(refined_in.vertices.attributes(), ATTR_QUAD_EDGE_STEP);
            const GEO::Attribute<int> qe(refined_in.vertices.attributes(), ATTR_QUAD_EDGE);
            /* the first quad edge's polyline has to come out of the file unchanged */
            const SurfaceLayout::QuadEdge &first = layout.quad_edges.front();
            size_t found = 0;
            for (size_t i = 0; i < first.vertices.size(); ++i) {
                const int v = first.vertices[i];
                if (v >= 0 && (GEO::index_t)v < refined_in.vertices.nb()
                        && step[v] >= 0 && qe[v] == 0 && (size_t)step[v] == i)
                    ++found;
            }
            check(found == first.vertices.size(),
                    "the polyline of quad edge 0 round-trips through the file",
                    std::to_string(found) + " of " + std::to_string(first.vertices.size()));
        }
    }

    {
        const bool have_qe = GEO::Attribute<int>::is_defined(
                refined_in.edges.attributes(), ATTR_QUAD_EDGE);
        check(have_qe, "edge attribute quad_edge is present",
                std::to_string(refined_in.edges.nb()) + " edges");
        if (have_qe) {
            const GEO::Attribute<int> qe(refined_in.edges.attributes(), ATTR_QUAD_EDGE);
            size_t marked = 0;
            for (GEO::index_t e = 0; e < refined_in.edges.nb(); ++e)
                if (qe[e] >= 0) ++marked;
            check(marked > 0, "refined mesh edges are marked with their quad edge",
                    std::to_string(marked) + " of " + std::to_string(refined_in.edges.nb()));
        }
    }

    GEO::Mesh quads_in;
    check(loadBack(quad_path, quads_in), "quad mesh can be read back");
    check(quads_in.facets.nb() == (GEO::index_t)quadMesh.n_faces(),
            "quad mesh faces survived",
            std::to_string(quads_in.facets.nb()) + " of " + std::to_string(quadMesh.n_faces()));
    {
        const bool have_cell = GEO::Attribute<int>::is_defined(
                quads_in.facets.attributes(), ATTR_CELL);
        check(have_cell, "quad mesh facet attribute cell is present");
        if (have_cell) {
            const GEO::Attribute<int> cell(quads_in.facets.attributes(), ATTR_CELL);
            size_t with_cell = 0;
            for (GEO::index_t f = 0; f < quads_in.facets.nb(); ++f)
                if (cell[f] >= 0) ++with_cell;
            check(with_cell > 0, "quad faces know their cell",
                    std::to_string(with_cell) + " of " + std::to_string(quads_in.facets.nb()));
        }
    }
    {
        const bool have_qe2 = GEO::Attribute<int>::is_defined(
                quads_in.edges.attributes(), ATTR_QUAD_EDGE);
        check(have_qe2, "quad mesh edge attribute quad_edge is present");
        if (have_qe2) {
            const GEO::Attribute<int> qe(quads_in.edges.attributes(), ATTR_QUAD_EDGE);
            const bool have_border = GEO::Attribute<unsigned int>::is_defined(
                    quads_in.edges.attributes(), ATTR_BORDER);
            const GEO::Attribute<unsigned int> border(quads_in.edges.attributes(), ATTR_BORDER);
            std::map<int, int> per_quad_edge;
            size_t border_edges = 0, interior_without = 0;
            for (GEO::index_t e = 0; e < quads_in.edges.nb(); ++e) {
                if (qe[e] >= 0) ++per_quad_edge[qe[e]];
                if (have_border && border[e]) ++border_edges;
                else if (qe[e] < 0) ++interior_without;
            }
            /* a quad edge that runs through the interior of a quad face (a slit of
             * a face with double edges) or that is coincident with another quad
             * edge is not an edge of the quad mesh, so the layout can have more
             * quad edges than the mesh has edges */
            check(interior_without == 0,
                    "every interior quad mesh edge carries its quad edge",
                    "without " + std::to_string(interior_without) + ", border edges "
                            + std::to_string(border_edges) + ", layout quad edges "
                            + std::to_string(layout.quad_edges.size()) + " on "
                            + std::to_string(per_quad_edge.size()) + " mesh edges");
        }
    }

    std::cout << "=== written ===" << std::endl;
    std::cout << "  " << refined_path << "  (subdivision, all attributes)" << std::endl;
    std::cout << "  " << quad_path << "  (quad mesh, all attributes)" << std::endl;
    if (failures) {
        std::cout << failures << " check(s) FAILED." << std::endl;
        return 5;
    }
    std::cout << "All checks passed." << std::endl;
    return 0;
}
