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
 * Two meshes are provided. The attribute names are short on purpose: the element
 * an attribute belongs to (vertex, edge, facet, facet corner) is part of its
 * identity, both in memory and in a geogram file, where the attributes are
 * grouped per element type. An attribute that can be "not there" is an `int`
 * holding -1; attributes that always have a value are `unsigned int`.
 *
 * @par toRefinedMesh() - the polygon mesh that is a subdivision of the input
 *      triangle mesh (cut along all quad edge polylines):
 *
 *      | element      | attribute       | type         | meaning                                   |
 *      |--------------|-----------------|--------------|-------------------------------------------|
 *      | vertex       | `kind`          | unsigned int | 0 = triangle vertex, 1 = point on a        |
 *      |              |                 |              | triangle edge, 2 = grid vertex             |
 *      | vertex       | `tri_vertex`    | int          | triangle mesh vertex, -1                   |
 *      | vertex       | `tri_edge`      | int          | triangle mesh edge, -1                     |
 *      | vertex       | `grid_vertex`   | int          | grid vertex, -1                            |
 *      | vertex       | `cell`          | int          | an incident cell, -1                       |
 *      | vertex       | `quad_edge`     | int          | quad edge the vertex lies on, -1           |
 *      | vertex       | `quad_edge_step`| int          | position on its polyline, -1               |
 *      | edge         | `quad_edge`     | int          | quad edge the edge lies on, -1             |
 *      | edge         | `tri_edge`      | int          | triangle mesh edge, -1                     |
 *      | facet        | `tri_face`      | unsigned int | triangle mesh face the facet is part of    |
 *      | facet        | `cell`          | unsigned int | cell (region) the facet belongs to         |
 *      | facet        | `quad_face`     | int          | quad mesh face of that cell, -1 if none    |
 *      | facet        | `tri_faces_nb`  | unsigned int | triangle faces covered by the cell         |
 *      | facet corner | `quad_edge`     | int          | quad edge of the edge starting here, -1    |
 *
 *      `cell` is a region of the surface partition: all facets of a cell form one
 *      patch of the triangle mesh, and `tri_faces_nb` is the number of triangle
 *      mesh faces that patch covers (i.e. the number of distinct `tri_face`
 *      values in it). A cell inside one of the extractor's holes has no quad mesh
 *      face, which `quad_face` reports as -1.
 *
 *      The polyline of a quad edge *is* the set of edges carrying that
 *      `quad_edge`; its vertices, ordered by `quad_edge_step`, are the polyline.
 *
 * @par toQuadMesh() - the extracted quad mesh (one face per cell):
 *
 *      | element   | attribute      | type         | meaning                                    |
 *      |-----------|----------------|--------------|--------------------------------------------|
 *      | facet     | `cell`         | int          | the cell of this face, -1                  |
 *      | facet     | `poly_face`    | int          | poly mesh face before merging, -1          |
 *      | facet     | `quad_face`    | unsigned int | the facet's own index (identity)           |
 *      | facet     | `tri_faces_nb` | unsigned int | triangle faces covered by the cell         |
 *      | edge      | `quad_edge`    | int          | the quad edge (with its polyline), -1      |
 *      | edge      | `border`       | unsigned int | 1 if the edge has no quad edge             |
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
