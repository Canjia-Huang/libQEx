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

#include "qex.h"
#include "../../src/MeshExtractorT.hh"
#include "../../src/QuadExtractorPostprocT.hh"
#include "../../src/Globals.hh"

#include <iostream>
#include <algorithm>
#include <iterator>
#include <cassert>

namespace QEx {
void extractPolyMesh(TriMesh_t in_triMesh, const_UVVector_t in_uvs, const_ValenceVector_t in_vertexValences,
                     QuadMesh_t out_quadMesh, QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager &heLocalUvProp) {

    /*
     * Convert UV representation.
     */
    std::vector<double> uvs; uvs.reserve(in_uvs->size() * 2);
    for (std::vector<OpenMesh::Vec2d>::const_iterator it = in_uvs->begin(), it_end = in_uvs->end();
            it_end != it; ++it) {
        uvs.push_back((*it)[0]);
        uvs.push_back((*it)[1]);
    }

    out_quadMesh->request_face_status();
    out_quadMesh->request_halfedge_status();
    out_quadMesh->request_edge_status();
    out_quadMesh->request_vertex_status();

    QEx::MeshExtractorT<TriMesh> qme(*in_triMesh);
    qme.extract<QuadMesh>(uvs, heLocalUvProp, *out_quadMesh, in_vertexValences);
}

void mergePolyToQuad(QuadMesh_t inout_polyMesh, QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager &heLocalUvProp, std::vector<int> *out_poly_to_quad) {
    QEx::QuadExtractorPostprocT<QuadMesh> qexPp(*inout_polyMesh, heLocalUvProp);
    qexPp.ngons_to_quads(out_poly_to_quad);

    inout_polyMesh->garbage_collection();

    inout_polyMesh->release_face_status();
    inout_polyMesh->release_halfedge_status();
    inout_polyMesh->release_edge_status();
    inout_polyMesh->release_vertex_status();
}

void extractQuadMeshWithLayout(TriMesh_t in_triMesh, const_UVVector_t in_uvs,
                               const_ValenceVector_t in_vertexValences,
                               QuadMesh_t out_quadMesh, QEx::SurfaceLayout *out_layout,
                               QuadMesh_t out_refinedMesh, bool in_mergeToQuads) {

    /*
     * Convert UV representation.
     */
    std::vector<double> uvs; uvs.reserve(in_uvs->size() * 2);
    for (std::vector<OpenMesh::Vec2d>::const_iterator it = in_uvs->begin(), it_end = in_uvs->end();
            it_end != it; ++it) {
        uvs.push_back((*it)[0]);
        uvs.push_back((*it)[1]);
    }

    out_quadMesh->request_face_status();
    out_quadMesh->request_halfedge_status();
    out_quadMesh->request_edge_status();
    out_quadMesh->request_vertex_status();

    QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager heLocalUvProp(
            *out_quadMesh, QEx::QExGlobals::LOCAL_UVS_HANDLE_NAME());

    QEx::MeshExtractorT<TriMesh> qme(*in_triMesh);

    QuadMesh fallbackRefinedMesh;
    if (out_refinedMesh) {
        out_refinedMesh->clear();
        out_refinedMesh->request_face_status();
        out_refinedMesh->request_vertex_status();
    }

    typename QEx::MeshExtractorT<TriMesh>::Layout layout;
    qme.extract_with_layout<QuadMesh, QuadMesh>(uvs, heLocalUvProp, *out_quadMesh,
            out_refinedMesh ? *out_refinedMesh : fallbackRefinedMesh, layout, in_vertexValences);

    if (out_layout) {
        SurfaceLayout &l = *out_layout;
        l = SurfaceLayout();

        l.vertex_position = layout.vertex_position;
        l.vertex_kind = layout.vertex_kind;
        l.vertex_tri_vertex = layout.vertex_tri_vertex;
        l.vertex_tri_edge = layout.vertex_tri_edge;
        l.vertex_grid_vertex = layout.vertex_grid_vertex;
        l.face_vertices = layout.face_vertices;
        l.face_tri_face = layout.face_tri_face;
        l.face_cell = layout.face_cell;
        l.face_edge_quad_edge = layout.face_edge_quad_edge;

        l.n_poly_faces = layout.n_poly_faces;
        l.n_cells = layout.n_cells;
        l.cell_poly_face = layout.cell_poly_face;
        l.cell_tri_faces = layout.cell_tri_faces;
        l.cell_quad_edges = layout.cell_quad_edges;
        l.cell_vertices = layout.cell_vertices;

        l.n_degenerate_triangles = layout.n_degenerate_triangles;
        l.n_unresolved_segments = layout.n_unresolved_segments;
        l.n_cell_poly_face_conflicts = layout.n_cell_poly_face_conflicts;
        l.n_failed_refined_faces = layout.n_failed_refined_faces;
        l.n_pruned_pieces = layout.n_pruned_pieces;
        l.n_degenerate_quad_edges = layout.n_degenerate_quad_edges;
        l.n_degenerate_pieces = layout.n_degenerate_pieces;
        l.n_collapsed_quad_edges = layout.n_collapsed_quad_edges;
        l.n_coincident_quad_edges = layout.n_coincident_quad_edges;
        l.n_cells_without_poly_face = layout.n_cells_without_poly_face;
        l.n_poly_faces_without_cell = layout.n_poly_faces_without_cell;
        l.n_desired_holes = layout.n_desired_holes;
        l.n_undesired_holes = layout.n_undesired_holes;

        l.quad_edges.resize(layout.quad_edges.size());
        for (size_t i = 0; i < layout.quad_edges.size(); ++i) {
            const typename QEx::MeshExtractorT<TriMesh>::QuadEdge &src = layout.quad_edges[i];
            SurfaceLayout::QuadEdge &dst = l.quad_edges[i];
            dst.gv_a = src.gv_a;
            dst.gv_b = src.gv_b;
            dst.poly_face_a = src.poly_face_a;
            dst.poly_face_b = src.poly_face_b;
            dst.poly_halfedge_a = src.poly_halfedge_a;
            dst.poly_halfedge_b = src.poly_halfedge_b;
            dst.cell_left = src.cell_left;
            dst.cell_right = src.cell_right;
            dst.vertices = src.step_vertices;
            dst.points.reserve(src.step_vertices.size());
            for (size_t k = 0; k < src.step_vertices.size(); ++k) {
                const int v = src.step_vertices[k];
                if (v >= 0 && (size_t)v < layout.vertex_position.size())
                    dst.points.push_back(layout.vertex_position[v]);
            }
            dst.pieces.reserve(src.steps.size());
            for (size_t k = 0; k < src.steps.size(); ++k) {
                SurfaceLayout::Piece piece;
                piece.tri_face = src.steps[k].fh.idx();
                piece.uv_in = OpenMesh::Vec2d(src.steps[k].uv_in[0], src.steps[k].uv_in[1]);
                piece.uv_out = OpenMesh::Vec2d(src.steps[k].uv_out[0], src.steps[k].uv_out[1]);
                dst.pieces.push_back(piece);
            }
        }
    }

    if (in_mergeToQuads) {
        /*
         * The merging reports which face of the resulting quad mesh replaced which
         * face of the poly mesh, so the correspondence is taken from there instead
         * of being guessed from the local uvs (which cannot be reliable: the
         * merging moves the vertices by averaging them, and degenerate faces can
         * share their corner coordinates).
         */
        std::vector<int> poly_to_quad;
        mergePolyToQuad(out_quadMesh, heLocalUvProp, out_layout ? &poly_to_quad : 0);

        if (out_layout) {
            SurfaceLayout &l = *out_layout;
            l.cell_quad_face.assign(l.n_cells, -1);
            for (int c = 0; c < l.n_cells; ++c) {
                const int poly_face = l.cell_poly_face[c];
                if (poly_face < 0 || poly_face >= (int)poly_to_quad.size()) continue;
                l.cell_quad_face[c] = poly_to_quad[poly_face];
            }
        }
    } else {
        out_quadMesh->garbage_collection();
        out_quadMesh->release_face_status();
        out_quadMesh->release_halfedge_status();
        out_quadMesh->release_edge_status();
        out_quadMesh->release_vertex_status();
    }

    if (out_refinedMesh) {
        out_refinedMesh->garbage_collection();
        out_refinedMesh->release_face_status();
        out_refinedMesh->release_vertex_status();
    }
}
}

extern "C" {

namespace QEx {
void extractQuadMeshOM(TriMesh_t in_triMesh, const_UVVector_t in_uvs, const_ValenceVector_t in_vertexValences, QuadMesh_t out_quadMesh) {

    // Create temporary local UVs property.
    QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager heLocalUvProp(
            *out_quadMesh, QEx::QExGlobals::LOCAL_UVS_HANDLE_NAME());

    extractPolyMesh(in_triMesh, in_uvs, in_vertexValences, out_quadMesh, heLocalUvProp);
    mergePolyToQuad(out_quadMesh, heLocalUvProp);
}

}

void qex_extractQuadMesh(qex_TriMesh const * in_triMesh, qex_Valence *in_vertexValences, qex_QuadMesh * out_quadMesh) {

    /*
     * Convert input into OpenMesh format.
     */
    std::vector<unsigned int> valences, *valences_ptr = 0;
    if (in_vertexValences != 0) {
        valences.reserve(in_triMesh->vertex_count);
        std::copy(in_vertexValences, in_vertexValences + in_triMesh->vertex_count, std::back_inserter(valences));
        valences_ptr = &valences;
    }

    /*
     * This estimate is accurate for genus 1 meshes. For all other reasonable meshes
     * it shouldn't be off by too much.
     */
    const unsigned int estimatedEdgeCount = in_triMesh->vertex_count + in_triMesh->tri_count;
    QEx::TriMesh triMesh;
    triMesh.reserve(in_triMesh->vertex_count, estimatedEdgeCount, in_triMesh->tri_count);

    /*
     * Transfer vertices.
     */
    for (qex_Point3 *vtx_it = in_triMesh->vertices, *vtx_end = in_triMesh->vertices + in_triMesh->vertex_count; vtx_it != vtx_end; ++vtx_it) {
        triMesh.add_vertex(QEx::TriMesh::Point(vtx_it->x[0], vtx_it->x[1], vtx_it->x[2]));
    }

    /*
     * Transfer faces (implicitly creating the edges).
     */
    for (qex_Tri *tri_it = in_triMesh->tris, *tri_end = in_triMesh->tris + in_triMesh->tri_count; tri_it != tri_end; ++tri_it) {
        QEx::TriMesh::FaceHandle fh = triMesh.add_face(
            triMesh.vertex_handle(tri_it->indices[0]),
            triMesh.vertex_handle(tri_it->indices[1]),
            triMesh.vertex_handle(tri_it->indices[2]));


    }

    /*
     * Transfer UVs.
     */
    std::vector<OpenMesh::Vec2d> uvs; uvs.resize(triMesh.n_halfedges());
    QEx::TriMesh::FaceIter f_it = triMesh.faces_begin();
    for (qex_UVTri *uvTri_it = in_triMesh->uvTris, *uvTri_end = in_triMesh->uvTris + in_triMesh->tri_count; uvTri_it != uvTri_end; ++uvTri_it, ++f_it) {
        qex_Point2 *uv_it = uvTri_it->uvs;
        for (QEx::TriMesh::FHIter fh_it = triMesh.fh_begin(*f_it), fh_end = triMesh.fh_end(*f_it); fh_it != fh_end; ++fh_it, ++uv_it) {
            uvs[fh_it->idx()] = OpenMesh::Vec2d(uv_it->x[0], uv_it->x[1]);
        }
    }

    QEx::QuadMesh quadMesh;
    QEx::extractQuadMeshOM(&triMesh, &uvs, valences_ptr, &quadMesh);

    /*
     * Convert output back into raw format.
     */

    /*
     * Transfer vertices.
     */
    out_quadMesh->vertex_count = quadMesh.n_vertices();
    out_quadMesh->vertices = static_cast<qex_Point3*>(malloc(sizeof(qex_Point3) * out_quadMesh->vertex_count));
    qex_Point3 *out_vtx = out_quadMesh->vertices;
    for (QEx::QuadMesh::VertexIter v_it = quadMesh.vertices_begin(), v_end = quadMesh.vertices_end(); v_it != v_end; ++v_it, ++out_vtx) {
        const QEx::QuadMesh::Point cur_point = quadMesh.point(*v_it);
        std::copy(cur_point.data(), cur_point.data() + 3, out_vtx->x);
    }

    /*
     * Transfer faces.
     */
    out_quadMesh->quad_count = quadMesh.n_faces();
    out_quadMesh->quads = static_cast<qex_Quad*>(malloc(sizeof(qex_Quad) * out_quadMesh->quad_count));
    qex_Quad *out_quad = out_quadMesh->quads;
    size_t nonQuadFaces = 0;
    for (QEx::QuadMesh::FaceIter f_it = quadMesh.faces_begin(), f_end = quadMesh.faces_end(); f_it != f_end; ++f_it, ++out_quad) {
        if (quadMesh.valence(*f_it) != 4) {
            ++nonQuadFaces;
            continue;
        }
        assert(quadMesh.valence(*f_it) == 4);
        QEx::QuadMesh::FaceVertexIter fv_it = quadMesh.fv_begin(*f_it);
        qex_Index *out_quad_idx = out_quad->indices;
        for (int i = 0; i < 4; ++i, ++fv_it, ++out_quad_idx) {
            assert(fv_it != quadMesh.fv_end(*f_it));
            assert(fv_it->is_valid());
            *out_quad_idx = static_cast<unsigned int>(fv_it->idx());
        }
    }

    if (nonQuadFaces > 0)
        std::cerr << "Skipped " << nonQuadFaces << " non-quad faces." << std::endl;
}

} /* extern "C" */
