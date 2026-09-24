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
#ifndef QEX_GEOGRAM_H_INCLUDED
#define QEX_GEOGRAM_H_INCLUDED

#include <qex.h>

#include <geogram/basic/common.h>
#include <geogram/mesh/mesh.h>

#include <string>

namespace QEx {

/**
 * @brief Conversion of the extraction result into geogram meshes.
 *
 * Everything the extraction produces is stored in geogram *attributes*, so that
 * the result can be written to any format geogram's mesh IO supports
 * (.geogram, .mesh, .obj, .ply, .stl, ...) without losing the information.
 *
 * Two meshes are provided:
 *
 * @par toRefinedMesh() - the polygon mesh that is a subdivision of the input
 *      triangle mesh (cut along all quad edge polylines). Attributes:
 *
 *      vertices:
 *      - `qex_kind`            0 = triangle mesh vertex, 1 = point inserted on a
 *                              triangle mesh edge, 2 = grid vertex
 *      - `qex_tri_vertex`      index of the triangle mesh vertex, else -1
 *      - `qex_tri_edge`        index of the triangle mesh edge a point was
 *                              inserted on, else -1
 *      - `qex_grid_vertex`     index of the grid vertex, else -1
 *      - `qex_quad_edge`       quad edge the vertex belongs to, else -1
 *      - `qex_quad_edge_step`  position of the vertex along that quad edge's
 *                              polyline, else -1
 *      - `qex_cell`            most frequent cell of the incident faces, else -1
 *
 *      facets:
 *      - `qex_tri_face`        triangle mesh face this facet is part of
 *      - `qex_cell`            cell (== face of the extracted quad mesh) the
 *                              facet belongs to, -1 inside the extractor's holes
 *      - `qex_quad_face`       face of the final quad mesh that cell maps to, -1
 *      - `qex_tri_faces`       number of triangle mesh faces covered by the cell
 *
 *      facet corners (one per edge of a facet):
 *      - `qex_corner_quad_edge`  quad edge the edge starting at this corner lies
 *                                on, -1 if the edge is not part of a quad edge
 *
 *      edges:
 *      - `qex_quad_edge`       quad edge this edge lies on, -1 if none. Together
 *                              with `qex_quad_edge_step` this *is* the polyline
 *                              of a quad edge: its vertices are exactly the
 *                              vertices carrying that quad edge index, ordered by
 *                              `qex_quad_edge_step`.
 *      - `qex_tri_edge`        triangle mesh edge this edge lies on, else -1
 *
 * @par toQuadMesh() - the extracted quad mesh (one face per cell). Attributes:
 *
 *      facets:
 *      - `qex_cell`            cell index (== face of the poly mesh before merging)
 *      - `qex_poly_face`       face of the poly mesh before merging
 *      - `qex_quad_face`       the facet's own index (identity, kept for symmetry)
 *      - `qex_tri_face_count`  number of triangle mesh faces the cell covers
 *
 *      edges:
 *      - `qex_quad_edge`       index of the quad edge (see SurfaceLayout::quad_edges)
 *      - `qex_border`          1 if this quad edge borders a hole/ the boundary
 *
 * @see QEx::SurfaceLayout, QEx::extractQuadMeshWithLayout
 */
class GeogramBridge {
    public:
        /**
         * @brief Initializes geogram.
         *
         * Called automatically by the conversion functions; call it explicitly if
         * you want to use geogram itself before/after the conversion.
         */
        static void initialize();

        /**
         * @brief Fills @p out with the refined mesh, i.e. the subdivision of the
         *        input triangle mesh, including all attributes listed above.
         */
        static void toRefinedMesh(const SurfaceLayout &layout, GEO::Mesh &out);

        /**
         * @brief Fills @p out with the extracted quad mesh and its attributes.
         *
         * @param layout the layout the quad mesh was extracted with
         * @param quadMesh the quad mesh, e.g. the one extractQuadMeshWithLayout()
         *        wrote
         */
        static void toQuadMesh(const SurfaceLayout &layout, const QuadMesh &quadMesh,
                GEO::Mesh &out);

        /**
         * @brief Writes a mesh with geogram's mesh IO.
         *
         * The format is chosen from the file name's extension, e.g. `.geogram`,
         * `.mesh`, `.obj`, `.ply`, `.stl`. All attributes created by this class
         * survive in the formats that can store them (`.geogram` and `.mesh` can,
         * `.obj` and `.stl` cannot).
         *
         * @return true on success
         */
        static bool save(const GEO::Mesh &mesh, const std::string &filename);

        /// Convenience: refine + save (see toRefinedMesh() and save()).
        static bool saveRefinedMesh(const SurfaceLayout &layout, const std::string &filename);

        /// Convenience: quad mesh + save (see toQuadMesh() and save()).
        static bool saveQuadMesh(const SurfaceLayout &layout, const QuadMesh &quadMesh,
                const std::string &filename);

        /**
         * @brief Human readable list of the attributes of a mesh, one per line.
         *
         * Useful to check what actually made it into a file.
         */
        static std::string describe(const GEO::Mesh &mesh);
};

} // namespace QEx

#endif // QEX_GEOGRAM_H_INCLUDED
