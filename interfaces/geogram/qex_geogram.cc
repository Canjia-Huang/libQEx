/*
 * Copyright 2013 Computer Graphics Group, RWTH Aachen University
 * Author: Hans-Christian Ebke <ebke@cs.rwth-aachen.de>
 *
 * This file is part of QEx.
 *
 * QEx is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * QEx is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with QEx.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qex_geogram.h"

#include <geogram/basic/attributes.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh_io.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace QEx {

namespace {

/** Attribute names, so that consumers and tests use the very same strings. */
namespace Attr {
const char *const KIND = "qex_kind";
const char *const TRI_VERTEX = "qex_tri_vertex";
const char *const TRI_EDGE = "qex_tri_edge";
const char *const GRID_VERTEX = "qex_grid_vertex";
const char *const QUAD_EDGE = "qex_quad_edge";
const char *const QUAD_EDGE_STEP = "qex_quad_edge_step";
const char *const CELL = "qex_cell";
const char *const TRI_FACE = "qex_tri_face";
const char *const QUAD_FACE = "qex_quad_face";
const char *const TRI_FACES = "qex_tri_faces";
const char *const TRI_FACE_COUNT = "qex_tri_face_count";
const char *const CORNER_QUAD_EDGE = "qex_corner_quad_edge";
const char *const POLY_FACE = "qex_poly_face";
const char *const BORDER = "qex_border";
} // namespace Attr

/**
 * @brief A vertex index of a quad edge inside a cell loop, used to stitch the
 *        polylines of a cell without relying on the direction the layout stored.
 */
inline std::pair<int, int> edge_key(int a, int b) {
    return std::make_pair(std::min(a, b), std::max(a, b));
}

} // namespace

void GeogramBridge::initialize() {
    static bool initialized = false;
    if (!initialized) {
        GEO::initialize();
        /*
         * geogram's ".geogram" writer asks for "sys:compression_level" (and other
         * system settings) while saving. Those variables only exist once the
         * corresponding argument group has been imported, and geogram asserts if
         * one of them is missing - so import the standard group here.
         */
        GEO::CmdLine::import_arg_group("standard");
        initialized = true;
    }
}

void GeogramBridge::toRefinedMesh(const SurfaceLayout &layout, GEO::Mesh &out) {
    initialize();

    out.clear(false, true);
    const GEO::index_t n_vertices = (GEO::index_t)layout.vertex_position.size();
    out.vertices.create_vertices(n_vertices);
    for (GEO::index_t v = 0; v < n_vertices; ++v) {
        const OpenMesh::Vec3d &p = layout.vertex_position[v];
        out.vertices.point(v) = GEO::vec3(p[0], p[1], p[2]);
    }

    /* ---------------- attributes of the vertices ---------------- */
    GEO::Attribute<int> kind(out.vertices.attributes(), Attr::KIND);
    GEO::Attribute<int> tri_vertex(out.vertices.attributes(), Attr::TRI_VERTEX);
    GEO::Attribute<int> tri_edge(out.vertices.attributes(), Attr::TRI_EDGE);
    GEO::Attribute<int> grid_vertex(out.vertices.attributes(), Attr::GRID_VERTEX);
    GEO::Attribute<int> vertex_quad_edge(out.vertices.attributes(), Attr::QUAD_EDGE);
    GEO::Attribute<int> vertex_quad_edge_step(out.vertices.attributes(), Attr::QUAD_EDGE_STEP);
    GEO::Attribute<int> vertex_cell(out.vertices.attributes(), Attr::CELL);

    for (GEO::index_t v = 0; v < n_vertices; ++v) {
        kind[v] = layout.vertex_kind[v];
        tri_vertex[v] = layout.vertex_tri_vertex[v];
        tri_edge[v] = layout.vertex_tri_edge[v];
        grid_vertex[v] = layout.vertex_grid_vertex[v];
        vertex_quad_edge[v] = -1;
        vertex_quad_edge_step[v] = -1;
        vertex_cell[v] = -1;
    }

    /*
     * The polylines of the quad edges: the chain of a quad edge is a sequence of
     * refined vertices, and every vertex of that chain knows its position in it.
     * (A vertex where several quad edges meet - a grid vertex - keeps the first
     * quad edge that claims it; the edges of the mesh carry the full information.)
     */
    for (size_t q = 0; q < layout.quad_edges.size(); ++q) {
        const SurfaceLayout::QuadEdge &qe = layout.quad_edges[q];
        for (size_t i = 0; i < qe.vertices.size(); ++i) {
            const int v = qe.vertices[i];
            if (v < 0 || (size_t)v >= n_vertices) continue;
            if (vertex_quad_edge[v] >= 0 && vertex_quad_edge_step[v] >= 0) continue;
            vertex_quad_edge[v] = (int)q;
            vertex_quad_edge_step[v] = (int)i;
        }
    }

    /* ---------------- facets ---------------- */
    /* the attributes of a facet are known before the facet is created, so they
     * are collected first and written afterwards */
    std::vector<int> facet_tri_face, facet_cell, facet_quad_face, facet_tri_faces;
    std::vector<int> corner_quad_edge;

    /*
     * cell -> number of covered triangle faces and cell -> quad face
     */
    std::vector<int> cell_tri_face_count(layout.n_cells, 0);
    for (size_t c = 0; c < layout.cell_tri_faces.size(); ++c)
        cell_tri_face_count[c] = (int)layout.cell_tri_faces[c].size();
    std::vector<int> cell_quad_face(layout.n_cells, -1);
    for (size_t c = 0; c < layout.cell_quad_face.size() && c < layout.cell_quad_face.size(); ++c)
        if (c < cell_quad_face.size()) cell_quad_face[c] = layout.cell_quad_face[c];

    /* refined mesh edge (as a sorted vertex pair) -> quad edge */
    std::map<std::pair<int, int>, int> quad_edge_of_edge;
    for (size_t q = 0; q < layout.quad_edges.size(); ++q) {
        const SurfaceLayout::QuadEdge &qe = layout.quad_edges[q];
        for (size_t i = 0; i + 1 < qe.vertices.size(); ++i) {
            const int a = qe.vertices[i], b = qe.vertices[i + 1];
            if (a < 0 || b < 0 || a == b) continue;
            const std::pair<int, int> key = edge_key(a, b);
            if (!quad_edge_of_edge.count(key)) quad_edge_of_edge[key] = (int)q;
        }
    }

    for (size_t f = 0; f < layout.face_vertices.size(); ++f) {
        const std::vector<int> &face = layout.face_vertices[f];
        if (face.size() < 3) continue;

        const GEO::index_t facet = out.facets.create_polygon((GEO::index_t)face.size());
        for (size_t i = 0; i < face.size(); ++i)
            out.facets.set_vertex(facet, (GEO::index_t)i, (GEO::index_t)face[i]);

        const int cell = layout.face_cell[f];
        facet_tri_face.push_back(layout.face_tri_face[f]);
        facet_cell.push_back(cell);
        facet_quad_face.push_back(cell >= 0 && cell < (int)cell_quad_face.size()
                ? cell_quad_face[cell] : -1);
        facet_tri_faces.push_back(cell >= 0 && cell < (int)cell_tri_face_count.size()
                ? cell_tri_face_count[cell] : 0);

        /* per edge of the facet: the quad edge it lies on, or -1 */
        for (size_t i = 0; i < face.size(); ++i) {
            const int a = face[i], b = face[(i + 1) % face.size()];
            const std::map<std::pair<int, int>, int>::const_iterator it =
                    quad_edge_of_edge.find(edge_key(a, b));
            corner_quad_edge.push_back(it == quad_edge_of_edge.end() ? -1 : it->second);
        }
    }

    GEO::Attribute<int> facet_tri_face_attr(out.facets.attributes(), Attr::TRI_FACE);
    GEO::Attribute<int> facet_cell_attr(out.facets.attributes(), Attr::CELL);
    GEO::Attribute<int> facet_quad_face_attr(out.facets.attributes(), Attr::QUAD_FACE);
    GEO::Attribute<int> facet_tri_faces_attr(out.facets.attributes(), Attr::TRI_FACES);
    for (GEO::index_t f = 0; f < out.facets.nb(); ++f) {
        facet_tri_face_attr[f] = facet_tri_face[f];
        facet_cell_attr[f] = facet_cell[f];
        facet_quad_face_attr[f] = facet_quad_face[f];
        facet_tri_faces_attr[f] = facet_tri_faces[f];
    }

    GEO::Attribute<int> corner_attr(out.facet_corners.attributes(), Attr::CORNER_QUAD_EDGE);
    geo_assert(out.facet_corners.nb() == corner_quad_edge.size());
    for (GEO::index_t c = 0; c < out.facet_corners.nb(); ++c)
        corner_attr[c] = corner_quad_edge[c];

    /* ---------------- edges ---------------- */
    /* dedicated edge connectivity, so that a quad edge can be looked up on edges */
    std::map<std::pair<int, int>, int> edge_index;
    std::vector<int> edge_quad_edge;
    for (size_t f = 0; f < layout.face_vertices.size(); ++f) {
        const std::vector<int> &face = layout.face_vertices[f];
        for (size_t i = 0; i < face.size(); ++i) {
            const int a = face[i], b = face[(i + 1) % face.size()];
            if (a < 0 || b < 0 || a == b) continue;
            const std::pair<int, int> key = edge_key(a, b);
            if (edge_index.count(key)) continue;
            const GEO::index_t e = out.edges.create_edge((GEO::index_t)key.first,
                    (GEO::index_t)key.second);
            edge_index[key] = (int)e;
            const std::map<std::pair<int, int>, int>::const_iterator it =
                    quad_edge_of_edge.find(key);
            while (edge_quad_edge.size() <= (size_t)e) edge_quad_edge.push_back(-1);
            edge_quad_edge[e] = it == quad_edge_of_edge.end() ? -1 : it->second;
        }
    }
    while (edge_quad_edge.size() < out.edges.nb()) edge_quad_edge.push_back(-1);

    GEO::Attribute<int> edge_quad_edge_attr(out.edges.attributes(), Attr::QUAD_EDGE);
    GEO::Attribute<int> edge_tri_edge_attr(out.edges.attributes(), Attr::TRI_EDGE);
    for (GEO::index_t e = 0; e < out.edges.nb(); ++e) {
        edge_quad_edge_attr[e] = edge_quad_edge[e];
        /* a triangle mesh edge is identified by the refined vertices that are its
         * triangle mesh vertices; the layout knows them for inserted points */
        const GEO::index_t v0 = out.edges.vertex(e, 0), v1 = out.edges.vertex(e, 1);
        int te = -1;
        if (layout.vertex_tri_edge[v0] >= 0 && layout.vertex_tri_edge[v0] == layout.vertex_tri_edge[v1])
            te = layout.vertex_tri_edge[v0];
        edge_tri_edge_attr[e] = te;
    }

    /* ---------------- vertex cell (majority of the incident facets) ---------------- */
    for (size_t f = 0; f < layout.face_vertices.size(); ++f) {
        const std::vector<int> &face = layout.face_vertices[f];
        const int cell = layout.face_cell[f];
        if (cell < 0) continue;
        for (size_t i = 0; i < face.size(); ++i)
            if (vertex_cell[face[i]] < 0) vertex_cell[face[i]] = cell;
    }
}

void GeogramBridge::toQuadMesh(const SurfaceLayout &layout, const QuadMesh &quadMesh,
        GEO::Mesh &out) {
    initialize();

    out.clear(false, true);
    out.vertices.create_vertices((GEO::index_t)quadMesh.n_vertices());
    for (QuadMesh::ConstVertexIter v = quadMesh.vertices_begin(); v != quadMesh.vertices_end(); ++v) {
        const QuadMesh::Point &p = quadMesh.point(*v);
        out.vertices.point((GEO::index_t)v->idx()) = GEO::vec3(p[0], p[1], p[2]);
    }

    /*
     * Facets are created in the order of the quad mesh's faces, so the facet
     * index *is* the quad mesh face index.
     */
    for (QuadMesh::ConstFaceIter f = quadMesh.faces_begin(); f != quadMesh.faces_end(); ++f) {
        std::vector<GEO::index_t> face;
        for (QuadMesh::ConstFaceVertexIter fv = quadMesh.cfv_begin(*f); fv.is_valid(); ++fv)
            face.push_back((GEO::index_t)fv->idx());
        if (face.size() < 3) continue;
        out.facets.create_polygon((GEO::index_t)face.size(), &face[0]);
    }

    /*
     * cell -> final quad mesh face, as computed by extractQuadMeshWithLayout().
     */
    std::vector<int> cell_of_facet((size_t)quadMesh.n_faces(), -1);
    for (size_t c = 0; c < layout.cell_quad_face.size(); ++c) {
        const int qf = layout.cell_quad_face[c];
        if (qf >= 0 && (size_t)qf < cell_of_facet.size()) cell_of_facet[qf] = (int)c;
    }

    GEO::Attribute<int> facet_cell_attr(out.facets.attributes(), Attr::CELL);
    GEO::Attribute<int> facet_poly_face_attr(out.facets.attributes(), Attr::POLY_FACE);
    GEO::Attribute<int> facet_quad_face_attr(out.facets.attributes(), Attr::QUAD_FACE);
    GEO::Attribute<int> facet_tri_face_count_attr(out.facets.attributes(), Attr::TRI_FACE_COUNT);
    for (GEO::index_t f = 0; f < out.facets.nb(); ++f) {
        const int cell = cell_of_facet[f];
        facet_cell_attr[f] = cell;
        facet_poly_face_attr[f] = cell >= 0 && (size_t)cell < layout.cell_poly_face.size()
                ? layout.cell_poly_face[cell] : -1;
        facet_quad_face_attr[f] = (int)f;
        facet_tri_face_count_attr[f] = cell >= 0 && (size_t)cell < layout.cell_tri_faces.size()
                ? (int)layout.cell_tri_faces[cell].size() : 0;
    }

    /*
     * Edge connectivity plus, for every edge, the two facets it belongs to.
     */
    std::map<std::pair<int, int>, int> edge_index;
    std::vector<std::vector<int> > facets_of_edge;
    for (GEO::index_t f = 0; f < out.facets.nb(); ++f) {
        const GEO::index_t n = out.facets.nb_vertices(f);
        for (GEO::index_t i = 0; i < n; ++i) {
            const int a = (int)out.facets.vertex(f, i);
            const int b = (int)out.facets.vertex(f, (i + 1) % n);
            if (a == b) continue;
            const std::pair<int, int> key = edge_key(a, b);
            std::map<std::pair<int, int>, int>::iterator it = edge_index.find(key);
            GEO::index_t e;
            if (it == edge_index.end()) {
                e = out.edges.create_edge((GEO::index_t)key.first, (GEO::index_t)key.second);
                edge_index[key] = (int)e;
                facets_of_edge.resize((size_t)e + 1);
            } else {
                e = (GEO::index_t)it->second;
            }
            facets_of_edge[e].push_back((int)f);
        }
    }

    /*
     * A quad edge of the layout separates two cells; those cells are the two
     * facets of one quad mesh edge. Keying by the pair of facets therefore links
     * the layout's quad edges (with their polylines) to the quad mesh's edges,
     * independently of any renumbering the merging step did.
     */
    std::map<std::pair<int, int>, int> quad_edge_of_facet_pair;
    for (size_t q = 0; q < layout.quad_edges.size(); ++q) {
        const SurfaceLayout::QuadEdge &qe = layout.quad_edges[q];
        if (qe.cell_left < 0 || qe.cell_right < 0 || qe.cell_left == qe.cell_right) continue;
        if ((size_t)qe.cell_left >= layout.cell_quad_face.size()
                || (size_t)qe.cell_right >= layout.cell_quad_face.size())
            continue;
        const int f0 = layout.cell_quad_face[qe.cell_left];
        const int f1 = layout.cell_quad_face[qe.cell_right];
        if (f0 < 0 || f1 < 0) continue;
        quad_edge_of_facet_pair[std::make_pair(std::min(f0, f1), std::max(f0, f1))] = (int)q;
    }

    GEO::Attribute<int> edge_quad_edge_attr(out.edges.attributes(), Attr::QUAD_EDGE);
    GEO::Attribute<int> edge_border_attr(out.edges.attributes(), Attr::BORDER);
    for (GEO::index_t e = 0; e < out.edges.nb(); ++e) {
        int q = -1;
        const std::vector<int> &incident = facets_of_edge[e];
        if (incident.size() == 2) {
            const std::map<std::pair<int, int>, int>::const_iterator it =
                    quad_edge_of_facet_pair.find(std::make_pair(
                            std::min(incident[0], incident[1]), std::max(incident[0], incident[1])));
            if (it != quad_edge_of_facet_pair.end()) q = it->second;
        }
        edge_quad_edge_attr[e] = q;
        edge_border_attr[e] = (q < 0) ? 1 : 0;
    }
}

bool GeogramBridge::save(const GEO::Mesh &mesh, const std::string &filename) {
    initialize();
    return GEO::mesh_save(mesh, filename);
}

bool GeogramBridge::saveRefinedMesh(const SurfaceLayout &layout, const std::string &filename) {
    GEO::Mesh mesh;
    toRefinedMesh(layout, mesh);
    return save(mesh, filename);
}

bool GeogramBridge::saveQuadMesh(const SurfaceLayout &layout, const QuadMesh &quadMesh,
        const std::string &filename) {
    GEO::Mesh mesh;
    toQuadMesh(layout, quadMesh, mesh);
    return save(mesh, filename);
}

std::string GeogramBridge::describe(const GEO::Mesh &mesh) {
    std::ostringstream os;
    os << "vertices: " << mesh.vertices.nb() << ", edges: " << mesh.edges.nb()
       << ", facets: " << mesh.facets.nb() << ", facet corners: " << mesh.facet_corners.nb()
       << std::endl;

    struct StoreListing {
            static void append(std::ostream &os, const char *what,
                    const GEO::AttributesManager &manager) {
                GEO::vector<std::string> names;
                manager.list_attribute_names(names);
                std::sort(names.begin(), names.end());
                os << "  " << what << ":";
                for (GEO::index_t i = 0; i < names.size(); ++i) {
                    const std::string name = names[i];
                    const GEO::AttributeStore *store = manager.find_attribute_store(name);
                    os << " " << name;
                    if (store) os << "[" << store->dimension() << "]";
                }
                os << std::endl;
            }
    };
    StoreListing::append(os, "vertex attributes", mesh.vertices.attributes());
    StoreListing::append(os, "edge attributes", mesh.edges.attributes());
    StoreListing::append(os, "facet attributes", mesh.facets.attributes());
    StoreListing::append(os, "facet corner attributes", mesh.facet_corners.attributes());

    return os.str();
}

} // namespace QEx
