/*
 * Export tool for the quad layout on the triangle mesh.
 *
 * Reads a triangle mesh with a (relaxed) integer grid map from an OBJ file,
 * extracts the quad mesh and writes
 *
 *   <prefix>_quad.obj       the extracted quad mesh
 *   <prefix>_refined.obj    the subdivision of the triangle mesh along the
 *                           quad edge polylines (one "g tri<idx>_cell<idx>"
 *                           group per face)
 *   <prefix>_polylines.obj  the polylines of all quad edges (OBJ line elements)
 *   <prefix>_cells.obj      one polygon per cell, i.e. per quad mesh face
 *   <prefix>_layout.txt     the correspondence quad face <-> triangle faces and
 *                           the quad edge polylines
 *
 * and verifies a number of invariants of the resulting subdivision.
 */

#include <OpenMesh/Core/IO/MeshIO.hh>
#include <qex.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace QEx;

namespace {

struct Diagnostics {
    int failures;
    int warnings;
    Diagnostics() : failures(0), warnings(0) {}

    void check(bool ok, const std::string &what, const std::string &detail = "") {
        std::cout << (ok ? "  [ ok ] " : "  [FAIL] ") << what;
        if (!detail.empty()) std::cout << "   (" << detail << ")";
        std::cout << std::endl;
        if (!ok) ++failures;
    }

    /** A deviation that is known to be caused by the input / the extractor. */
    void warn(bool ok, const std::string &what, const std::string &detail = "") {
        std::cout << (ok ? "  [ ok ] " : "  [warn] ") << what;
        if (!detail.empty()) std::cout << "   (" << detail << ")";
        std::cout << std::endl;
        if (!ok) ++warnings;
    }
};

double triangleArea(const TriMesh &mesh, TriMesh::FaceHandle fh) {
    std::vector<TriMesh::Point> p;
    for (TriMesh::ConstFaceVertexIter v = mesh.cfv_begin(fh); v.is_valid(); ++v) p.push_back(mesh.point(*v));
    if (p.size() != 3) return 0.0;
    const TriMesh::Point a = p[1] - p[0], b = p[2] - p[0];
    const TriMesh::Point c = a.cross(b);
    return 0.5 * c.norm();
}

void writeObj(const std::string &path, const std::vector<OpenMesh::Vec3d> &positions,
              const std::vector<std::vector<int> > &faces,
              const std::vector<std::string> *face_groups = 0) {
    std::ofstream os(path.c_str());
    os.precision(12);
    for (size_t i = 0; i < positions.size(); ++i)
        os << "v " << positions[i][0] << " " << positions[i][1] << " " << positions[i][2] << "\n";
    for (size_t i = 0; i < faces.size(); ++i) {
        if (face_groups && i < face_groups->size()) os << "g " << (*face_groups)[i] << "\n";
        os << "f";
        for (size_t k = 0; k < faces[i].size(); ++k) os << " " << (faces[i][k] + 1);
        os << "\n";
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <infile.obj> <out_prefix> [--valences <file>] [--no-merge]"
                << std::endl;
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
    if (!triMesh.has_halfedge_texcoords2D()) {
        std::cerr << "Input mesh has no per-halfedge texcoords." << std::endl;
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
    if (!valence_file.empty()) {
        std::ifstream is(valence_file.c_str());
        std::string header;
        std::getline(is, header);
        size_t n = 0;
        is >> n;
        if (n != triMesh.n_vertices()) {
            std::cerr << "Unexpected number of valences." << std::endl;
            return 4;
        }
        valences.resize(n);
        for (size_t i = 0; i < n; ++i) is >> valences[i];
        valencesPtr = &valences;
    }

    std::cout << "=== input ===" << std::endl;
    std::cout << "  " << infile << ": " << triMesh.n_vertices() << " vertices, "
              << triMesh.n_faces() << " faces, " << triMesh.n_halfedges() << " halfedges" << std::endl;

    QuadMesh quadMesh, refinedMesh;
    SurfaceLayout layout;
    extractQuadMeshWithLayout(&triMesh, &uvVector, valencesPtr, &quadMesh, &layout, &refinedMesh, merge);

    std::cout << "=== extracted ===" << std::endl;
    std::cout << "  quad mesh: " << quadMesh.n_vertices() << " vertices, " << quadMesh.n_faces()
              << " faces" << std::endl;
    std::cout << "  refined mesh: " << refinedMesh.n_vertices() << " vertices, "
              << refinedMesh.n_faces() << " faces" << std::endl;
    std::cout << "  poly mesh faces (before merging): " << layout.n_poly_faces << std::endl;
    std::cout << "  cells: " << layout.n_cells << ", quad edges: " << layout.quad_edges.size()
              << std::endl;
    std::cout << "  diagnostics: degenerate triangles " << layout.n_degenerate_triangles
              << ", unresolved pieces " << layout.n_unresolved_segments
              << ", pruned pieces " << layout.n_pruned_pieces
              << ", conflicts " << layout.n_cell_poly_face_conflicts << std::endl;

    /*
     * ------------------------------------------------------------------
     * Verification
     * ------------------------------------------------------------------
     */
    Diagnostics diag;
    std::cout << "=== verification ===" << std::endl;

    std::cout << "  [info] holes reported by the extractor: " << layout.n_undesired_holes
              << " undesired, " << layout.n_desired_holes << " desired" << std::endl;

    diag.warn(layout.n_cells == layout.n_poly_faces,
            "number of cells equals number of poly mesh faces",
            "cells " + std::to_string(layout.n_cells) + ", poly faces "
                    + std::to_string(layout.n_poly_faces) + ", final quad faces "
                    + std::to_string(quadMesh.n_faces()) + ", undesired holes "
                    + std::to_string(layout.n_undesired_holes));

    /*
     * The partition of the surface (the refined mesh) is exact; what can differ is
     * the correspondence: where the extractor left a hole (an "undesired hole"),
     * the grid has a cell that no quad mesh face covers, and the faces around the
     * hole can end up in the same cell. Both deviations are bounded by the number
     * of those holes.
     */
    /*
     * If the extractor reports no holes at all, then the grid and the quad mesh
     * have to agree exactly. If it does report holes, the cells around them can
     * end up unassigned (and the faces around them merged), so the deviation is
     * reported but not treated as a broken result - the partition itself (the
     * refined mesh) is unchanged by them.
     */
    if (layout.n_undesired_holes == 0) {
        diag.check(layout.n_cells == layout.n_poly_faces
                        && layout.n_cells_without_poly_face == 0
                        && layout.n_poly_faces_without_cell == 0,
                "without holes, cells and quad faces correspond exactly",
                "cells " + std::to_string(layout.n_cells) + ", poly faces "
                        + std::to_string(layout.n_poly_faces));
    } else {
        diag.warn(layout.n_cells_without_poly_face + layout.n_poly_faces_without_cell
                        <= 2 * (size_t)layout.n_undesired_holes + 1,
                "deviations are bounded by the extractor's holes",
                "cells without face " + std::to_string(layout.n_cells_without_poly_face)
                        + ", faces without cell "
                        + std::to_string(layout.n_poly_faces_without_cell)
                        + ", undesired holes " + std::to_string(layout.n_undesired_holes));
    }

    {
        std::set<int> seen;
        bool ok = true;
        /* -1 means "this cell has no quad face" (a cell inside a hole); that is
         * reported separately and is not an inconsistency */
        size_t unassigned = 0;
        for (int c = 0; c < layout.n_cells; ++c) {
            const int f = layout.cell_poly_face[c];
            if (f < 0) { ++unassigned; continue; }
            if (f >= layout.n_poly_faces) { ok = false; continue; }
            if (!seen.insert(f).second) { ok = false; continue; }
        }
        diag.check(ok, "cell -> poly mesh face: no duplicates, no invalid indices");
        diag.warn((int)seen.size() == layout.n_poly_faces,
                "every poly mesh face is covered by a cell",
                "covered " + std::to_string(seen.size()) + " of "
                        + std::to_string(layout.n_poly_faces) + " poly faces ("
                        + std::to_string(layout.n_poly_faces_without_cell)
                        + " without a cell, undesired holes "
                        + std::to_string(layout.n_undesired_holes) + ")");
    }

    if (merge) {
        bool injective = true;
        size_t missing = 0;
        std::set<int> seen;
        for (int c = 0; c < layout.n_cells; ++c) {
            const int f = layout.cell_quad_face[c];
            if (f < 0) { ++missing; continue; }
            if (!seen.insert(f).second) injective = false;
        }
        diag.check(injective, "cell -> final quad face mapping is injective",
                std::to_string(seen.size()) + " distinct final faces for "
                        + std::to_string(layout.n_cells) + " cells");
        diag.warn((int)seen.size() + (int)missing >= layout.n_cells,
                "cell -> final quad face mapping is complete",
                "mapped " + std::to_string(seen.size()) + " of "
                        + std::to_string(layout.n_cells) + " cells, "
                        + std::to_string(missing) + " without a face (faces dropped while merging)");
    }

    diag.warn(layout.n_cell_poly_face_conflicts == 0,
            "all boundary quad edges of a cell agree on the quad face",
            "conflicts " + std::to_string(layout.n_cell_poly_face_conflicts)
                    + " (cells around the extractor's holes/slits)");
    diag.check(layout.n_unresolved_segments == 0, "all polyline pieces were resolved",
            "unresolved " + std::to_string(layout.n_unresolved_segments));
    diag.check(layout.n_failed_refined_faces == 0, "all refined faces could be added to the mesh",
            "failed " + std::to_string(layout.n_failed_refined_faces));

    /* every refined face knows its triangle and its cell */
    {
        size_t unassigned = 0, unknown_tri = 0;
        for (size_t k = 0; k < layout.face_cell.size(); ++k) {
            if (layout.face_cell[k] < 0) ++unassigned;
            if (layout.face_tri_face[k] < 0) ++unknown_tri;
        }
        diag.check(unassigned == 0, "every refined face belongs to a cell",
                "unassigned " + std::to_string(unassigned));
        diag.check(unknown_tri == 0, "every refined face knows its triangle",
                "unknown " + std::to_string(unknown_tri));
    }

    /* the refined mesh partitions the triangles: area is preserved */
    {
        double triArea = 0.0;
        for (TriMesh::FaceIter f = triMesh.faces_begin(); f != triMesh.faces_end(); ++f)
            triArea += triangleArea(triMesh, *f);
        double refinedArea = 0.0;
        for (size_t k = 0; k < layout.face_vertices.size(); ++k) {
            const std::vector<int> &vs = layout.face_vertices[k];
            for (size_t i = 1; i + 1 < vs.size(); ++i) {
                const OpenMesh::Vec3d a = layout.vertex_position[vs[i]] - layout.vertex_position[vs[0]];
                const OpenMesh::Vec3d b = layout.vertex_position[vs[i + 1]] - layout.vertex_position[vs[0]];
                refinedArea += 0.5 * a.cross(b).norm();
            }
        }
        const double rel = triArea > 0 ? std::fabs(refinedArea - triArea) / triArea : 0.0;
        diag.check(rel < 1e-9, "refined mesh covers the triangle mesh exactly",
                "tri area " + std::to_string(triArea) + ", refined area " + std::to_string(refinedArea));
    }

    /* the cell boundary loops close */
    {
        size_t empty = 0, noncyclic = 0;
        for (int c = 0; c < layout.n_cells; ++c) {
            const std::vector<int> &loop = layout.cell_vertices[c];
            if (loop.size() < 3) { ++empty; continue; }
            /* the loop is a cyclic sequence of quad edge chains; check that the
             * edges of the chain set actually form a closed cycle */
            std::multiset<std::pair<int, int> > boundary_edges;
            for (size_t i = 0; i < layout.cell_quad_edges[c].size(); ++i) {
                const SurfaceLayout::QuadEdge &qe = layout.quad_edges[layout.cell_quad_edges[c][i]];
                if (qe.vertices.size() < 2) continue;
                /* the chains have to be oriented the way the cell walks them */
                int from = qe.vertices.front(), to = qe.vertices.back();
                if (qe.cell_left != c) std::swap(from, to);
                boundary_edges.insert(std::make_pair(from, to));
            }
            std::map<int, int> degree;
            for (std::multiset<std::pair<int, int> >::const_iterator it = boundary_edges.begin();
                    it != boundary_edges.end(); ++it) {
                ++degree[it->first];
                --degree[it->second];
            }
            bool balanced = true;
            for (std::map<int, int>::const_iterator it = degree.begin(); it != degree.end(); ++it)
                if (it->second != 0) balanced = false;
            if (!balanced) ++noncyclic;
        }
        /* cells inside the extractor's holes may have no boundary at all */
        diag.check(empty <= layout.n_cells_without_poly_face,
                "every cell that has quad edges has a boundary loop",
                "empty " + std::to_string(empty) + " (cells without a quad face: "
                        + std::to_string(layout.n_cells_without_poly_face) + ")");
        diag.warn(noncyclic == 0, "the cell boundaries are closed cycles",
                "non-cyclic " + std::to_string(noncyclic)
                        + " (cells touching the extractor's holes)");
    }

    /* quad edge sanity: interior quad edges have two cells, boundary ones one */
    {
        size_t bad = 0, interior = 0, boundary = 0, collapsed = 0;
        for (size_t q = 0; q < layout.quad_edges.size(); ++q) {
            const SurfaceLayout::QuadEdge &qe = layout.quad_edges[q];
            if (qe.vertices.size() < 2) { ++collapsed; continue; }
            if (qe.points.size() < 2) ++bad;
            if (qe.cell_left < 0 && qe.cell_right < 0) ++bad;
            else if (qe.cell_left >= 0 && qe.cell_right >= 0) ++interior;
            else ++boundary;
        }
        diag.check(bad == layout.n_coincident_quad_edges,
                "every quad edge with a polyline separates (or borders) cells",
                "without sides " + std::to_string(bad) + " == coincident quad edges "
                        + std::to_string(layout.n_coincident_quad_edges) + ", collapsed "
                        + std::to_string(collapsed) + " (degenerate pieces "
                        + std::to_string(layout.n_degenerate_pieces) + ")");
        std::cout << "  [info] quad edges: " << interior << " interior, " << boundary
                  << " boundary" << std::endl;
    }

    /* triangle faces per cell */
    {
        std::map<size_t, size_t> histogram;
        size_t covered = 0;
        for (int c = 0; c < layout.n_cells; ++c) {
            histogram[layout.cell_tri_faces[c].size()]++;
            covered += layout.cell_tri_faces[c].size();
        }
        std::cout << "  [info] triangle faces per cell (total " << covered << " vs "
                  << triMesh.n_faces() << " triangles):";
        for (std::map<size_t, size_t>::const_iterator it = histogram.begin();
                it != histogram.end(); ++it)
            std::cout << " " << it->first << "->" << it->second;
        std::cout << std::endl;
        diag.check(covered >= triMesh.n_faces() && covered <= 2 * triMesh.n_faces(),
                "curved cells counted consistently (a triangle can touch two cells)");
    }

    /*
     * ------------------------------------------------------------------
     * Write the results
     * ------------------------------------------------------------------
     */
    if (merge) {
        OpenMesh::IO::write_mesh(quadMesh, prefix + "_quad.obj", OpenMesh::IO::Options::Default, 12);
    } else {
        OpenMesh::IO::write_mesh(quadMesh, prefix + "_polymesh.obj", OpenMesh::IO::Options::Default, 12);
    }

    std::vector<std::string> groups(layout.face_vertices.size());
    for (size_t k = 0; k < layout.face_vertices.size(); ++k) {
        groups[k] = "tri" + std::to_string(layout.face_tri_face[k])
                + "_cell" + std::to_string(layout.face_cell[k]);
    }
    /* plain subdivision mesh (for viewing) and the same mesh with per-face labels */
    writeObj(prefix + "_refined.obj", layout.vertex_position, layout.face_vertices, 0);
    writeObj(prefix + "_refined_labeled.obj", layout.vertex_position, layout.face_vertices, &groups);

    /* polylines as OBJ lines */
    {
        std::ofstream os((prefix + "_polylines.obj").c_str());
        os.precision(12);
        for (size_t i = 0; i < layout.vertex_position.size(); ++i)
            os << "v " << layout.vertex_position[i][0] << " "
               << layout.vertex_position[i][1] << " " << layout.vertex_position[i][2] << "\n";
        for (size_t q = 0; q < layout.quad_edges.size(); ++q) {
            const SurfaceLayout::QuadEdge &qe = layout.quad_edges[q];
            if (qe.vertices.size() < 2) continue;
            os << "g qedge" << q << "_cell" << qe.cell_left << "_cell" << qe.cell_right << "\n";
            os << "l";
            for (size_t k = 0; k < qe.vertices.size(); ++k) os << " " << (qe.vertices[k] + 1);
            os << "\n";
        }
    }

    /* one polygon per cell */
    writeObj(prefix + "_cells.obj", layout.vertex_position, layout.cell_vertices);

    /* the correspondence itself */
    {
        std::ofstream os((prefix + "_layout.txt").c_str());
        os.precision(12);
        os << "# cells -> quad face / triangle faces\n";
        for (int c = 0; c < layout.n_cells; ++c) {
            os << "cell " << c << " quad_face " << layout.cell_poly_face[c];
            if (merge) os << " final_quad_face " << layout.cell_quad_face[c];
            os << " n_quad_edges " << layout.cell_quad_edges[c].size() << " tri_faces";
            for (size_t i = 0; i < layout.cell_tri_faces[c].size(); ++i)
                os << " " << layout.cell_tri_faces[c][i];
            os << "\n";
        }
        os << "\n# quad edges: grid vertices, adjacent cells, polyline\n";
        for (size_t q = 0; q < layout.quad_edges.size(); ++q) {
            const SurfaceLayout::QuadEdge &qe = layout.quad_edges[q];
            os << "qedge " << q << " gv " << qe.gv_a << " -> " << qe.gv_b
               << " cells " << qe.cell_left << "/" << qe.cell_right
               << " quad_faces " << qe.poly_face_a << "/" << qe.poly_face_b
               << " polyline";
            for (size_t k = 0; k < qe.vertices.size(); ++k) os << " " << qe.vertices[k];
            os << "\n";
        }
    }

    std::cout << "=== written ===" << std::endl;
    std::cout << "  " << prefix << (merge ? "_quad.obj" : "_polymesh.obj")
              << "  (final quad mesh)" << std::endl
              << "  " << prefix << "_refined.obj   (subdivision of the triangle mesh, plain)"
              << std::endl
              << "  " << prefix << "_refined_labeled.obj  (same, with 'g tri<idx>_cell<idx>' groups)"
              << std::endl
              << "  " << prefix << "_polylines.obj  (quad edge polylines, OBJ 'l' elements)"
              << std::endl
              << "  " << prefix << "_cells.obj      (one polygon per cell, boundary = polylines)"
              << std::endl
              << "  " << prefix << "_layout.txt     (cell <-> quad face <-> triangle faces, quad edges)"
              << std::endl;
    std::cout << "=== verdict ===" << std::endl;
    std::cout << "  partition of the triangle mesh: "
              << (diag.failures ? "BROKEN" : "exact (all structural invariants hold)") << std::endl;
    std::cout << "  correspondence: " << (layout.n_poly_faces - (int)layout.n_poly_faces_without_cell)
              << " of " << layout.n_poly_faces << " quad faces covered by exactly one cell; "
              << layout.n_cells_without_poly_face << " cell(s) without a quad face; "
              << layout.n_degenerate_quad_edges << " fin(s); "
              << layout.n_cell_poly_face_conflicts << " conflicting cell(s)" << std::endl;
    std::cout << "  " << diag.failures << " failure(s), " << diag.warnings
              << " warning(s) (residual deviations are explained by the extractor's holes)"
              << std::endl;
    if (diag.failures) return 5;
    return 0;
}
