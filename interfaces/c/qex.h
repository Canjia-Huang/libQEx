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

/// @file

#ifndef QEX_H_INCLUDED
#define QEX_H_INCLUDED

#ifndef DLLEXPORT
    #ifdef WIN32
        #ifdef QEX_EXPORT_SYMBOLS
            #define DLLEXPORT __declspec(dllexport)
        #else
            #define DLLEXPORT __declspec(dllimport)
        #endif
    #else
        #define DLLEXPORT
    #endif
#endif

#ifdef __cplusplus
#include <cstddef>
#include <OpenMesh/Core/Mesh/Traits.hh>
#include <OpenMesh/Core/Mesh/PolyMesh_ArrayKernelT.hh>
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>
#include <vector>


extern "C" {

namespace QEx {

struct PolyTraits : public OpenMesh::DefaultTraits {
        typedef OpenMesh::Vec3d Point;
        typedef OpenMesh::Vec3d Normal;
        typedef OpenMesh::Vec4f Color;
};

/// A shorthand for a suitable OpenMesh quad mesh type.
typedef OpenMesh::PolyMesh_ArrayKernelT<PolyTraits>  QuadMesh;

struct TriTraits : public OpenMesh::DefaultTraits {
    typedef OpenMesh::Vec3d Point;
    typedef OpenMesh::Vec3d Normal;
    typedef OpenMesh::Vec4f Color;
};

/// A shorthand for a suitable OpenMesh triangle mesh type.
typedef OpenMesh::TriMesh_ArrayKernelT<TriTraits>  TriMesh;

typedef QuadMesh *QuadMesh_t;
typedef TriMesh *TriMesh_t;
typedef const std::vector<unsigned int> *const_ValenceVector_t;
typedef const std::vector<OpenMesh::Vec2d> *const_UVVector_t;

/**
 * Extract a quad mesh from the given triangle mesh using the supplied per-halfedge UVs as
 * the (relaxed) integer grid map.
 *
 * @param in_triMesh The input triangle mesh.
 * @param in_uvs The per-halfedge UVs. Precondition: in_uvs->size() == in_triMesh->n_halfedges()
 * @param in_vertexValences The vertex valences. Optional. Precondition: in_vertexValences == 0 || in_vertexValences->size() == in_triMesh->n_vertices()
 * @param out_quadMesh The result will be output into the supplied quad mesh.
 */
DLLEXPORT void extractQuadMeshOM(TriMesh_t in_triMesh, const_UVVector_t in_uvs, const_ValenceVector_t in_vertexValences, QuadMesh_t out_quadMesh);

} /* namespace QEx */

#endif

typedef unsigned int qex_Index;
typedef unsigned int qex_Valence;

typedef struct {
    double x[3];
} qex_Point3;

typedef struct {
    double x[2];
} qex_Point2;

typedef struct {
    qex_Index indices[3];
} qex_Tri;

typedef struct {
    qex_Point2 uvs[3];
} qex_UVTri;

typedef struct {
    qex_Index indices[4];
} qex_Quad;

typedef struct {
    unsigned int vertex_count;
    unsigned int tri_count;

    /** Pointer to an array of vertex_count vertices. */
    qex_Point3 *vertices;

    /** Pointer to an array of tri_count triangles. */
    qex_Tri *tris;

    /** Pointer to an array of tri_count triangle UVs. */
    qex_UVTri *uvTris;
} qex_TriMesh;

typedef struct {
    unsigned int vertex_count;
    unsigned int quad_count;

    /** Pointer to an array of vertex_count vertices. */
    qex_Point3 *vertices;

    /** Pointer to an array of quad_count quads. */
    qex_Quad *quads;
} qex_QuadMesh;

/**
 * Extract a quad mesh from the given triangle mesh with a (relaxed) integer grid map.
 *
 * @param in_triMesh A pointer to the input triangle mesh.
 *
 * @param in_vertexValences May be NULL. If not NULL it has to point to an array of in_triMesh.vertex_count
 * integers each one corresponding to the valence of the given input vertex. If this argument is omitted, the
 * valence of each vertex is determined from the cross field which can lead to ambiguous results in certain
 * degenerate cases. If you know that your integer grid map is non-degenerate (i.e. contains no fold-overs and
 * triangles degenerated to a line or a point in the parameter domain) you can safely omit this argument.
 *
 * @param out_quadMesh A pointer to a qex_QuadMesh struct. The qex_QuadMesh has to be uninitialized.
 * It is the responsibility of the caller to free() the vertices and quads members that are returned.
 */
DLLEXPORT void qex_extractQuadMesh(qex_TriMesh const * in_triMesh, qex_Valence *in_vertexValences, qex_QuadMesh * out_quadMesh);

#ifdef __cplusplus
} // extern "C"

#include <OpenMesh/Core/Utils/Property.hh>
#include <OpenMesh/Core/Utils/PropertyManager.hh>
#include <OpenMesh/Core/Geometry/VectorT.hh>

namespace QEx {

typedef OpenMesh::VectorT<signed int,2> Vec2i;

/**
 * @brief Contains convenient declaration of property managers.
 */
template<typename MeshT>
class PropMgr {
    public:
        typedef OpenMesh::PropertyManager<OpenMesh::HPropHandleT<Vec2i>, MeshT> LocalUvsPropertyManager;
};

/**
 * Extract a poly mesh from the given triangle mesh using the supplied per-halfedge UVs as
 * the (relaxed) integer grid map. This performs all steps described in the paper up to and
 * including "Extracting Q-Faces". For perfect integer-grid parametrizations this already
 * generates a quad mesh. For relaxed integer-grid parametrizations this generates a mesh
 * which may contain n-gon faces (with n != 4).
 *
 * This function is mainly intended for debugging purposes. In most cases, extractQuadMeshOM()
 * is the function you want to call.
 *
 * @param in_triMesh @see extractQuadMeshOM()
 * @param in_uvs @see extractQuadMeshOM()
 * @param in_vertexValences @see extractQuadMeshOM()
 * @param out_quadMesh @see extractQuadMeshOM()
 * @param heLocalUvProp A property manager used to store the local UVs for later processing.
 */
DLLEXPORT
void extractPolyMesh(TriMesh_t in_triMesh, const_UVVector_t in_uvs,
                     const_ValenceVector_t in_vertexValences,
                     QuadMesh_t out_quadMesh,
                     QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager &heLocalUvProp);

/**
 * @brief The quad layout expressed as a subdivision of the input triangle mesh.
 *
 * All vertices and faces refer to a refined mesh which is a genuine subdivision
 * (a partition) of the input triangle mesh: it is obtained by cutting the
 * triangle mesh along the polylines of the quad edges. Every face of the refined
 * mesh lies completely inside exactly one cell, i.e. inside exactly one face of
 * the extracted quad mesh.
 *
 * @see extractQuadMeshWithLayout()
 */
struct SurfaceLayout {
        SurfaceLayout() : n_poly_faces(0), n_cells(0), n_degenerate_triangles(0), n_unresolved_segments(0),
                n_cell_poly_face_conflicts(0), n_failed_refined_faces(0), n_pruned_pieces(0),
                n_degenerate_quad_edges(0),
                n_degenerate_pieces(0), n_collapsed_quad_edges(0), n_coincident_quad_edges(0),
                n_cells_without_poly_face(0),
                n_poly_faces_without_cell(0), n_desired_holes(0), n_undesired_holes(0) {}

        /// one straight piece of a quad edge polyline
        struct Piece {
                /// triangle mesh face the piece lies in
                int tri_face;
                /// piece endpoints in the uv frame of that triangle
                OpenMesh::Vec2d uv_in, uv_out;
        };

        /// a quad edge: a connection between two grid vertices
        struct QuadEdge {
                QuadEdge() : gv_a(-1), gv_b(-1), poly_face_a(-1), poly_face_b(-1),
                        poly_halfedge_a(-1), poly_halfedge_b(-1), cell_left(-1), cell_right(-1) {}

                /// grid vertices connected by this quad edge
                int gv_a, gv_b;
                /// faces of the extracted quad mesh on either side of it
                int poly_face_a, poly_face_b;
                /// halfedges of the extracted quad mesh, -1 if unavailable
                int poly_halfedge_a, poly_halfedge_b;
                /// cells on the left / right hand side of the directed edge gv_a -> gv_b
                int cell_left, cell_right;
                /// indices into SurfaceLayout::vertex_position: the polyline of this quad edge
                std::vector<int> vertices;
                /// the same polyline as 3d points
                std::vector<OpenMesh::Vec3d> points;
                /// the straight pieces the polyline consists of
                std::vector<Piece> pieces;
        };

        // ---------- the refined mesh (subdivision of the triangle mesh) ----------
        std::vector<OpenMesh::Vec3d> vertex_position;
        /// 0: triangle mesh vertex, 1: point on a triangle mesh edge, 2: grid vertex
        std::vector<int> vertex_kind;
        std::vector<int> vertex_tri_vertex;
        std::vector<int> vertex_tri_edge;
        std::vector<int> vertex_grid_vertex;
        std::vector<std::vector<int> > face_vertices;
        /// per refined face: the triangle mesh face it belongs to
        std::vector<int> face_tri_face;
        /// per refined face: the cell (== quad mesh face) it belongs to
        std::vector<int> face_cell;
        /// per refined face: per edge, the quad edge it lies on, or -1
        std::vector<std::vector<int> > face_edge_quad_edge;

        // ---------- the correspondence ----------
        /// number of faces of the extracted poly mesh (before merging to quads)
        int n_poly_faces;
        /// number of cells
        int n_cells;
        /// per cell: the corresponding face of the extracted quad mesh
        std::vector<int> cell_poly_face;
        /**
         * Per cell: the corresponding face of the final quad mesh.
         *
         * Only meaningful if the merging step was run. It is obtained from the
         * mapping that mergePolyToQuad() reports while it merges (not from the
         * geometry, which would be unreliable because the merging moves vertices
         * by averaging them). -1 if the cell has no face of the quad mesh, which
         * is the case inside the extractor's holes.
         */
        std::vector<int> cell_quad_face;
        /// per cell: the triangle mesh faces covered by the cell
        std::vector<std::vector<int> > cell_tri_faces;
        /// per cell: the quad edges on the cell's boundary
        std::vector<std::vector<int> > cell_quad_edges;
        /// per cell: the cell's boundary as one cyclic sequence of vertices
        std::vector<std::vector<int> > cell_vertices;

        // ---------- quad edges (with their polylines) ----------
        std::vector<QuadEdge> quad_edges;

        // ---------- diagnostics ----------
        size_t n_degenerate_triangles;
        size_t n_unresolved_segments;
        size_t n_cell_poly_face_conflicts;
        size_t n_failed_refined_faces;
        /// polyline pieces that had to be pruned because they separate nothing
        size_t n_pruned_pieces;
        /// quad edges that do not separate two distinct quad mesh faces (fins)
        size_t n_degenerate_quad_edges;
        /// polyline pieces that collapsed to a single point (they separate nothing)
        size_t n_degenerate_pieces;
        /// quad edges whose polyline collapsed to a single point (nothing to separate)
        size_t n_collapsed_quad_edges;
        /**
         * Quad edges that run along exactly the same curve as another quad edge
         * (relaxed grid folds); they share that curve's cells.
         */
        size_t n_coincident_quad_edges;
        /**
         * Cells that correspond to no face of the quad mesh: QEx's face
         * construction did not build a face there (the extractor reports these as
         * "undesired holes"). Their cell_poly_face is -1.
         */
        size_t n_cells_without_poly_face;
        /// quad mesh faces that no cell is assigned to (should be 0)
        size_t n_poly_faces_without_cell;
        /// holes reported by the extractor (a desired hole is a hole of the input mesh)
        int n_desired_holes;
        /// holes the extractor itself created; they explain unassigned cells/faces
        int n_undesired_holes;
};

/**
 * @brief Extract the quad mesh together with the layout on the triangle mesh.
 *
 * In addition to what extractQuadMeshOM()/mergePolyToQuad() do, this outputs
 *
 *  - the polyline of every quad edge on the triangle mesh surface and
 *  - the subdivision of the triangle mesh along those polylines, including the
 *    correspondence between the quad mesh faces and the triangle mesh faces
 *    covered by them.
 *
 * @param in_triMesh @see extractQuadMeshOM()
 * @param in_uvs @see extractQuadMeshOM()
 * @param in_vertexValences @see extractQuadMeshOM()
 * @param out_quadMesh the extracted quad mesh
 * @param out_layout the layout; may be null
 * @param out_refinedMesh the refined mesh, i.e. the subdivision of the triangle
 *        mesh; may be null
 * @param in_mergeToQuads whether the "Vertex Merging"/"Q-Edge Recovery" steps
 *        should be run (true, i.e. like extractQuadMeshOM()) or whether the poly
 *        mesh as it comes out of extractPolyMesh() should be kept (false)
 */
DLLEXPORT
void extractQuadMeshWithLayout(TriMesh_t in_triMesh, const_UVVector_t in_uvs,
                               const_ValenceVector_t in_vertexValences,
                               QuadMesh_t out_quadMesh, QEx::SurfaceLayout *out_layout,
                               QuadMesh_t out_refinedMesh, bool in_mergeToQuads);

/**
 * Performs the "Vertex Merging" and "Q-Edge Recovery" steps described in the paper.
 * Executing extractPolyMesh() first and then this function is equivalent to
 * executing extractQuadMeshOM().
 *
 * @param inout_polyMesh The poly mesh which will be transformed into a quad mesh.
 * @param heLocalUvProp The local UVs used to perform the merging.
 * @param out_poly_to_quad Optional. Receives one entry per face of the poly mesh as
 *        it was *before* the merging: the index of the face of the resulting quad
 *        mesh that took its place, or -1 if the face disappeared. The merging is
 *        tracked, not guessed, so this mapping is exact.
 */
DLLEXPORT
void mergePolyToQuad(QuadMesh_t inout_polyMesh,
                     QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager &heLocalUvProp,
                     std::vector<int> *out_poly_to_quad = 0);

/**
 * @brief (Semi-)Generic version of extractQuadMeshOM(). Usable with different but identical traits.
 * @see extractQuadMeshOM
 */
template<typename TriMeshT, typename QuadMeshT>
inline void extractQuadMeshOMT(TriMeshT *in_triMesh, const_UVVector_t in_uvs,
                               const_ValenceVector_t in_vertexValences,
                               QuadMeshT *out_quadMesh) {
    extractQuadMeshOM(OpenMesh::MeshCast<TriMesh_t, TriMeshT*>::cast(in_triMesh),
                      in_uvs, in_vertexValences,
                      OpenMesh::MeshCast<QuadMesh_t, QuadMeshT*>::cast(out_quadMesh));
}

template<typename TriMeshT, typename QuadMeshT>
void extractPolyMeshT(TriMeshT *in_triMesh, const_UVVector_t in_uvs, const_ValenceVector_t in_vertexValences,
                      QuadMeshT *out_quadMesh, typename QEx::PropMgr<QuadMeshT>::LocalUvsPropertyManager &heLocalUvProp) {
    extractPolyMesh(OpenMesh::MeshCast<TriMesh_t, TriMeshT*>::cast(in_triMesh),
                    in_uvs, in_vertexValences,
                    OpenMesh::MeshCast<QuadMesh_t, QuadMeshT*>::cast(out_quadMesh),

                    /*
                     * This reinterpret_cast here might seem like an awful hack
                     * but since QuadMesh_T and QuadMeshT* are binary compatible
                     * (since otherwise the MeshCast above wouldn't compile)
                     * so must be the property managers of both types.
                     *
                     * From a software engineering point of view it would be
                     * nicer to provide an explicit cast in the PropertyManager
                     * class which uses the same mechanics as MeshCast to
                     * determine compatibility.
                     */
                    reinterpret_cast<typename QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager&>(heLocalUvProp));
}

template<typename QuadMeshT>
void mergePolyToQuadT(QuadMeshT *inout_polyMesh, typename QEx::PropMgr<QuadMeshT>::LocalUvsPropertyManager &heLocalUvProp) {
    mergePolyToQuad(OpenMesh::MeshCast<QuadMesh_t, QuadMeshT*>::cast(inout_polyMesh),

                    /*
                     * This reinterpret_cast here might seem like an awful hack
                     * but since QuadMesh_T and QuadMeshT* are binary compatible
                     * (since otherwise the MeshCast above wouldn't compile)
                     * so must be the property managers of both types.
                     *
                     * From a software engineering point of view it would be
                     * nicer to provide an explicit cast in the PropertyManager
                     * class which uses the same mechanics as MeshCast to
                     * determine compatibility.
                     */
                    reinterpret_cast<typename QEx::PropMgr<QuadMesh>::LocalUvsPropertyManager&>(heLocalUvProp));
}

} /* namespace QEx */

#endif // __cplusplus

#endif // QEX_H_INCLUDED
