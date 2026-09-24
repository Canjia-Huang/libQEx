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

/**
 * @file
 *
 * Reconstruction of the quad layout as a subdivision of the triangle mesh.
 *
 * While extracting the quad mesh, MeshExtractorT traces every quad edge (i.e.
 * every unit step of the integer grid) through the triangle mesh. Those traces
 * are recorded (see MeshExtractorT::PathStep), which allows us to
 *
 *  - output the polyline of every quad edge on the triangle mesh surface and
 *  - cut the triangle mesh along all those polylines, which yields a polygon
 *    mesh that is a genuine subdivision (a partition) of the input triangle
 *    mesh: every triangle is covered, neighbouring faces share their vertices
 *    and edges, and every face lies completely inside exactly one cell.
 *
 * The faces of the refined mesh are then grouped into cells by flood filling
 * across all edges that are not part of a quad edge polyline. Each cell is
 * exactly one face of the extracted poly/quad mesh, which yields the
 * correspondence
 *
 *      quad mesh face  <->  set of triangle mesh faces.
 *
 * @see MeshExtractorT::extract_with_layout
 */

#define QEX_SURFACEPARTITIONT_C

#include "MeshExtractorT.hh"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

namespace QEx {

namespace {

/**
 * @brief Whether the optional layout diagnostics should be printed.
 *
 * Set the environment variable QEX_LAYOUT_DIAGNOSTICS to a non-empty value to
 * get statistics about the arrangement, the flood fill and the correspondence
 * on stderr.
 */
inline bool layout_diagnostics_enabled() {
    static const bool enabled = (std::getenv("QEX_LAYOUT_DIAGNOSTICS") != 0);
    return enabled;
}

/**
 * @brief Face index of the face a halfedge (given by index) belongs to, -1 if unavailable.
 */
template<class MeshT>
inline int face_of_halfedge_index(const MeshT &_mesh, int _halfedge_index) {
    if (_halfedge_index < 0 || (size_t)_halfedge_index >= (size_t)_mesh.n_halfedges())
        return -1;
    const typename MeshT::HalfedgeHandle heh = _mesh.halfedge_handle(_halfedge_index);
    if (!heh.is_valid()) return -1;
    const typename MeshT::FaceHandle fh = _mesh.face_handle(heh);
    return fh.is_valid() ? fh.idx() : -1;
}

/**
 * @brief A planar arrangement of straight segments that only meet in their
 *        endpoints, used to split a single triangle.
 *
 * The triangle's boundary (already split at all crossing points) is inserted
 * first, then the pieces of the quad edge polylines passing through the
 * triangle. The faces of the arrangement except the outer one are the faces of
 * the refined mesh inside this triangle.
 */
class LocalArrangement {
    public:
        mutable size_t n_dropped_unclosed, n_dropped_orientation, n_dropped_missing_position;

        explicit LocalArrangement(const std::vector<int> &_boundary_loop) :
                boundary_loop_(_boundary_loop),
                n_dropped_unclosed(0), n_dropped_orientation(0), n_dropped_missing_position(0) {
            for (size_t i = 0; i < _boundary_loop.size(); ++i)
                add_edge(_boundary_loop[i], _boundary_loop[(i + 1) % _boundary_loop.size()], -1);
        }

        /**
         * Add an edge. Two polylines running along the very same segment are
         * collapsed into one edge; only the first payload is kept.
         */
        void add_edge(int _a, int _b, int _payload) {
            if (_a < 0 || _b < 0 || _a == _b) return;
            const std::pair<int, int> key(std::min(_a, _b), std::max(_a, _b));
            if (payload_of_.count(key)) return;
            payload_of_[key] = _payload;
            adj_[_a].push_back(_b);
            adj_[_b].push_back(_a);
        }

        /**
         * @brief Remove all edges that do not separate two regions.
         *
         * A polyline piece that is not part of a closed cell boundary (e.g.
         * because a neighbouring connection could not be traced) would
         * otherwise show up as a bridge in the arrangement: the face traversal
         * would walk it forth and back and thus visit a vertex twice. Removing
         * leaves iteratively leaves exactly the 2-edge-connected part of the
         * arrangement, whose edges all have a region on either side.
         *
         * @return the number of removed edges
         */
        size_t prune_leaves(std::set<std::pair<int, int> > *out_removed = 0) {
            std::vector<int> queue;
            for (std::map<int, std::vector<int> >::const_iterator it = adj_.begin();
                    it != adj_.end(); ++it)
                if (it->second.size() == 1) queue.push_back(it->first);

            size_t removed = 0;
            while (!queue.empty()) {
                const int v = queue.back();
                queue.pop_back();
                const std::map<int, std::vector<int> >::iterator vit = adj_.find(v);
                if (vit == adj_.end() || vit->second.size() != 1) continue;

                const int w = vit->second.front();
                if (out_removed)
                    out_removed->insert(std::pair<int, int>(std::min(v, w), std::max(v, w)));
                payload_of_.erase(std::pair<int, int>(std::min(v, w), std::max(v, w)));
                adj_.erase(vit);

                const std::map<int, std::vector<int> >::iterator wit = adj_.find(w);
                if (wit != adj_.end()) {
                    wit->second.erase(std::remove(wit->second.begin(), wit->second.end(), v),
                            wit->second.end());
                    if (wit->second.empty()) adj_.erase(wit);
                    else if (wit->second.size() == 1) queue.push_back(w);
                }
                ++removed;
            }
            return removed;
        }

        /**
         * @brief Extract the faces of the arrangement.
         *
         * @param _positions vertex positions in the triangle's uv frame, used
         *        for the angular ordering and the orientation test
         * @param _outer_directed_edge the directed boundary edge that belongs to
         *        the outer face; that face is dropped. Determining the outer
         *        face topologically (instead of by the sign of the uv area) is
         *        essential: in the relaxed parametrizations QEx has to deal
         *        with, a region can be self-overlapping in uv, which makes the
         *        area sign of an interior face unusable.
         * @param _out_faces the interior faces as cyclic vertex index lists
         * @param _out_face_edges for each face the payload of each of its edges;
         *        edge i connects vertex i with vertex i+1
         */
        void extract_faces(const std::map<int, std::pair<double, double> > &_positions,
                const std::pair<int, int> &_outer_directed_edge,
                std::vector<std::vector<int> > &_out_faces,
                std::vector<std::vector<int> > &_out_face_edges) const {
            _out_faces.clear();
            _out_face_edges.clear();

            /* sort the neighbours of every vertex by angle */
            std::map<int, std::vector<int> > neighbours;
            for (std::map<int, std::vector<int> >::const_iterator it = adj_.begin();
                    it != adj_.end(); ++it) {
                const int v = it->first;
                const std::map<int, std::pair<double, double> >::const_iterator pv =
                        _positions.find(v);
                if (pv == _positions.end()) continue;
                std::vector<std::pair<double, int> > order;
                for (size_t i = 0; i < it->second.size(); ++i) {
                    const std::map<int, std::pair<double, double> >::const_iterator pw =
                            _positions.find(it->second[i]);
                    if (pw == _positions.end()) { ++n_dropped_missing_position; continue; }
                    order.push_back(std::make_pair(
                            std::atan2(pw->second.second - pv->second.second,
                                    pw->second.first - pv->second.first), it->second[i]));
                }
                std::sort(order.begin(), order.end());
                for (size_t i = 0; i < order.size(); ++i) neighbours[v].push_back(order[i].second);
            }

            /*
             * Traverse the faces: arriving at v coming from u, continue with the
             * neighbour of v that follows next in clockwise direction, which
             * keeps the face on the left hand side.
             */
            std::set<std::pair<int, int> > visited;
            for (std::map<int, std::vector<int> >::const_iterator it = neighbours.begin();
                    it != neighbours.end(); ++it) {
                const int u_start = it->first;
                const std::vector<int> &nb = it->second;
                for (size_t k = 0; k < nb.size(); ++k) {
                    const int v_start = nb[k];
                    if (visited.count(std::make_pair(u_start, v_start))) continue;

                    std::vector<int> face;
                    std::vector<int> face_edges;
                    int u = u_start, v = v_start;
                    bool closed = false;
                    const size_t guard_max = 4 * (adj_.size() + 4) + 16;
                    for (size_t guard = 0; guard < guard_max; ++guard) {
                        visited.insert(std::make_pair(u, v));
                        face.push_back(u);
                        face_edges.push_back(payload(u, v));

                        const std::map<int, std::vector<int> >::const_iterator vn_it =
                                neighbours.find(v);
                        if (vn_it == neighbours.end()) break;
                        const std::vector<int> &vn = vn_it->second;
                        size_t idx = vn.size();
                        for (size_t i = 0; i < vn.size(); ++i)
                            if (vn[i] == u) { idx = i; break; }
                        if (idx == vn.size()) break;
                        const size_t next_idx = (idx + vn.size() - 1) % vn.size();
                        u = v;
                        v = vn[next_idx];
                        if (u == u_start && v == v_start) { closed = true; break; }
                    }
                    if (!closed || face.size() < 3) { ++n_dropped_unclosed; continue; }

                    bool ok = true;
                    for (size_t i = 0; i < face.size(); ++i)
                        if (!_positions.count(face[i])) { ok = false; break; }
                    if (!ok) { ++n_dropped_missing_position; continue; }

                    /* the outer face is the one that walks the triangle boundary
                     * in the direction opposite to the embedder's winding */
                    bool is_outer = false;
                    if (_outer_directed_edge.first >= 0) {
                        for (size_t i = 0; i < face.size(); ++i) {
                            if (face[i] == _outer_directed_edge.first
                                    && face[(i + 1) % face.size()] == _outer_directed_edge.second) {
                                is_outer = true;
                                break;
                            }
                        }
                    }
                    if (is_outer) { ++n_dropped_orientation; continue; }

                    _out_faces.push_back(face);
                    _out_face_edges.push_back(face_edges);
                }
            }
        }

    private:
        LocalArrangement(const LocalArrangement &);
        LocalArrangement &operator=(const LocalArrangement &);

        int payload(int _a, int _b) const {
            const std::map<std::pair<int, int>, int>::const_iterator it =
                    payload_of_.find(std::pair<int, int>(std::min(_a, _b), std::max(_a, _b)));
            return it == payload_of_.end() ? -1 : it->second;
        }

        std::vector<int> boundary_loop_;
        std::map<int, std::vector<int> > adj_;
        std::map<std::pair<int, int>, int> payload_of_;
};

/**
 * @brief Union-find for the cell flood fill.
 */
class UnionFind {
    public:
        explicit UnionFind(size_t _n) : parent_(_n) {
            for (size_t i = 0; i < _n; ++i) parent_[i] = i;
        }

        size_t find(size_t _a) {
            while (parent_[_a] != _a) {
                parent_[_a] = parent_[parent_[_a]];
                _a = parent_[_a];
            }
            return _a;
        }

        void unite(size_t _a, size_t _b) {
            const size_t ra = find(_a), rb = find(_b);
            if (ra != rb) parent_[rb] = ra;
        }

    private:
        std::vector<size_t> parent_;
};

} // namespace

template<class TMeshT>
template<class PolyMeshT, class LayoutMeshT>
void MeshExtractorT<TMeshT>::extract_with_layout(std::vector<double>& _uv_coords,
        typename PropMgr<PolyMeshT>::LocalUvsPropertyManager &heLocalUvProp,
        PolyMeshT& _quad_mesh, LayoutMeshT& _out_refined_mesh, Layout& _out_layout,
        const std::vector<unsigned int> * const _external_valences) {

    _out_layout = Layout();
    _out_refined_mesh.clear();

    extract(_uv_coords, heLocalUvProp, _quad_mesh, _external_valences);

    build_layout<PolyMeshT, LayoutMeshT>(_quad_mesh, _out_refined_mesh, _out_layout);
}

template<class TMeshT>
template<class PolyMeshT, class LayoutMeshT>
void MeshExtractorT<TMeshT>::build_layout(const PolyMeshT &_poly_mesh,
        LayoutMeshT &_out_refined_mesh, Layout &_out_layout) const {

    typedef typename TMesh::Point P3;
    const TMesh &tm = tri_mesh_;

    _out_layout.n_cells = 0;
    _out_layout.n_degenerate_triangles = 0;
    _out_layout.n_unresolved_segments = 0;
    _out_layout.n_cell_poly_face_conflicts = 0;
    _out_layout.n_failed_refined_faces = 0;
    _out_layout.n_pruned_pieces = 0;
    _out_layout.n_degenerate_quad_edges = 0;
    _out_layout.n_degenerate_pieces = 0;

    /* diagnostics */
    std::set<std::pair<int, int> > pruned_edges;
    size_t dropped_face_edges_shown = 0;
    size_t dropped_unclosed = 0, dropped_orientation = 0, dropped_missing = 0;
    size_t n_degenerate_pieces = 0;

    /*
     * ==================================================================
     * 1. Vertex registry of the refined mesh
     * ==================================================================
     */
    std::vector<P3> positions;
    std::vector<int> kind, tri_vertex, tri_edge, grid_vertex;

    /* the triangle mesh's own vertices */
    std::vector<int> vh_to_vertex(tm.n_vertices(), -1);
    const bool have_vertex_status = tm.has_vertex_status();
    for (typename TMesh::VertexIter v = tm.vertices_begin(); v != tm.vertices_end(); ++v) {
        if (have_vertex_status && tm.status(*v).deleted()) continue;
        if (tm.is_isolated(*v)) continue;
        vh_to_vertex[v->idx()] = (int)positions.size();
        positions.push_back(tm.point(*v));
        kind.push_back(0);
        tri_vertex.push_back(v->idx());
        tri_edge.push_back(-1);
        grid_vertex.push_back(-1);
    }

    std::vector<int> gv_to_vertex(gvertices_.size(), -1);

    /*
     * Points on triangle mesh edges. The parameter is measured in 3d and always
     * refers to the direction of halfedge 0 of the edge, which makes it
     * independent of the chart a point happens to be expressed in.
     */
    struct EdgePoint {
            double param;
            int vertex;
    };
    struct EdgePointLess {
            bool operator()(const EdgePoint &_a, const EdgePoint &_b) const {
                return _a.param < _b.param;
            }
    };
    std::vector<std::vector<EdgePoint> > edge_points(tm.n_edges());

    struct Registry {
            /// projection of a 3d point onto a triangle mesh edge
            static double edge_param(const TMesh &_tm, const MeshExtractorT<TMeshT> &_self,
                    typename TMesh::EdgeHandle _eh, const P3 &_p) {
                const typename TMesh::HalfedgeHandle heh0 = _tm.halfedge_handle(_eh, 0);
                const P3 a = _self.surface_point(_tm.prev_halfedge_handle(heh0));
                const P3 b = _self.surface_point(heh0);
                double len2 = 0.0, dot = 0.0;
                for (int i = 0; i < 3; ++i) {
                    len2 += (b[i] - a[i]) * (b[i] - a[i]);
                    dot += (b[i] - a[i]) * (_p[i] - a[i]);
                }
                if (len2 <= 0.0) return 0.0;
                return dot / len2;
            }

            /**
             * Register a point on a triangle mesh edge, or return the already
             * registered one. Points coinciding with a corner are mapped onto
             * the corner.
             */
            static int get(const TMesh &_tm, const MeshExtractorT<TMeshT> &_self,
                    std::vector<std::vector<EdgePoint> > &_edge_points,
                    const std::vector<int> &_vh_to_vertex, std::vector<P3> &_positions,
                    std::vector<int> &_kind, std::vector<int> &_tri_vertex,
                    std::vector<int> &_tri_edge, std::vector<int> &_grid_vertex,
                    typename TMesh::EdgeHandle _eh, double _param, const P3 &_p) {

                const typename TMesh::HalfedgeHandle heh0 = _tm.halfedge_handle(_eh, 0);
                if (_param <= 1e-9) {
                    const int v = _vh_to_vertex[_tm.to_vertex_handle(
                            _tm.prev_halfedge_handle(heh0)).idx()];
                    if (v >= 0) return v;
                }
                if (_param >= 1.0 - 1e-9) {
                    const int v = _vh_to_vertex[_tm.to_vertex_handle(heh0).idx()];
                    if (v >= 0) return v;
                }

                std::vector<EdgePoint> &pts = _edge_points[_eh.idx()];
                for (size_t i = 0; i < pts.size(); ++i)
                    if (std::fabs(pts[i].param - _param) < 1e-9)
                        return pts[i].vertex;

                const int id = (int)_positions.size();
                _positions.push_back(_p);
                _kind.push_back(1);
                _tri_vertex.push_back(-1);
                _tri_edge.push_back(_eh.idx());
                _grid_vertex.push_back(-1);
                EdgePoint ep; ep.param = _param; ep.vertex = id;
                pts.push_back(ep);
                return id;
            }
    };

    /* register all grid vertices */
    for (size_t i = 0; i < gvertices_.size(); ++i) {
        const GridVertex &gv = gvertices_[i];
        if (gv.type == GridVertex::OnVertex && gv.heh.is_valid()) {
            const VH vh = tm.to_vertex_handle(gv.heh);
            if (vh.is_valid() && vh_to_vertex[vh.idx()] >= 0) {
                gv_to_vertex[i] = vh_to_vertex[vh.idx()];
                grid_vertex[gv_to_vertex[i]] = (int)i;
                continue;
            }
        }
        if (gv.type == GridVertex::OnEdge && gv.heh.is_valid()) {
            const EH eh = tm.edge_handle(gv.heh);
            const double param = Registry::edge_param(tm, *this, eh, gv.position_3d);
            const int v = Registry::get(tm, *this, edge_points, vh_to_vertex, positions, kind,
                    tri_vertex, tri_edge, grid_vertex, eh, param, gv.position_3d);
            gv_to_vertex[i] = v;
            kind[v] = 2;
            grid_vertex[v] = (int)i;
            continue;
        }
        const int v = (int)positions.size();
        positions.push_back(gv.position_3d);
        kind.push_back(2);
        tri_vertex.push_back(-1);
        tri_edge.push_back(-1);
        grid_vertex.push_back((int)i);
        gv_to_vertex[i] = v;
    }

    /*
     * ==================================================================
     * 2. Quad edges and the crossing points of their polylines
     * ==================================================================
     */
    _out_layout.quad_edges.clear();

    /* refined mesh edge -> quad edge, keyed by the sorted pair of vertex indices */
    std::map<std::pair<int, int>, int> quad_edge_of_refined_edge;

    /*
     * Every traced quad edge separates cells; those that do not separate two
     * distinct faces of the poly mesh are counted as degenerate ("fins"):
     * QEx's add_face() accepts "double edge" faces, so a quad edge can end up
     * with the same face on both sides or with no face at all, in which case the
     * cell structure of the grid is finer than the poly mesh's face structure.
     */
    std::vector<char> quad_edge_is_separator;

    for (size_t i = 0; i < gvertices_.size(); ++i) {
        for (size_t j = 0; j < gvertices_[i].local_edges.size(); ++j) {
            const LocalEdgeInfo &lei = gvertices_[i].local_edges[j];
            if (lei.connected_to_idx < 0 || lei.path.empty()) continue;

            QuadEdge qe;
            qe.gv_a = (int)i;
            qe.gv_b = lei.connected_to_idx;
            qe.lei_a = (int)j;
            qe.steps = lei.path;
            qe.poly_halfedge_a = lei.halfedgeIndex;
            qe.poly_face_a = face_of_halfedge_index(_poly_mesh, lei.halfedgeIndex);
            if (lei.orientation_idx >= 0 &&
                    (size_t)lei.orientation_idx < gvertices_[qe.gv_b].local_edges.size()) {
                const LocalEdgeInfo &rev = gvertices_[qe.gv_b].local_edges[lei.orientation_idx];
                qe.poly_halfedge_b = rev.halfedgeIndex;
                qe.poly_face_b = face_of_halfedge_index(_poly_mesh, rev.halfedgeIndex);
            }

            const int qedge_index = (int)_out_layout.quad_edges.size();
            const bool is_separator = (qe.poly_face_a >= 0 && qe.poly_face_b >= 0
                    && qe.poly_face_a != qe.poly_face_b);
            quad_edge_is_separator.push_back(is_separator ? 1 : 0);
            if (!is_separator) ++_out_layout.n_degenerate_quad_edges;
            _out_layout.quad_edges.push_back(qe);
            QuadEdge &stored = _out_layout.quad_edges.back();

            /*
             * Turn the recorded trace into a chain of refined mesh vertices. The
             * entry point of a piece is the exit point of the previous piece, so
             * no geometric matching is needed at all.
             */
            int current = gv_to_vertex[stored.gv_a];
            stored.step_vertices.push_back(current);
            for (size_t s = 0; s < stored.steps.size(); ++s) {
                const PathStep &step = stored.steps[s];
                int next;
                if (step.exit_heh.is_valid()) {
                    const EH eh = tm.edge_handle(step.exit_heh);
                    const P3 p = surface_point(step.fh, step.uv_out);
                    const double param = Registry::edge_param(tm, *this, eh, p);
                    next = Registry::get(tm, *this, edge_points, vh_to_vertex, positions, kind,
                            tri_vertex, tri_edge, grid_vertex, eh, param, p);
                } else {
                    next = gv_to_vertex[stored.gv_b];
                }
                stored.step_vertices.push_back(next);
                if (current >= 0 && next >= 0 && current != next) {
                    const std::pair<int, int> key(std::min(current, next), std::max(current, next));
                    if (!quad_edge_of_refined_edge.count(key))
                        quad_edge_of_refined_edge[key] = qedge_index;
                }
                current = next;
            }
        }
    }

    for (size_t e = 0; e < edge_points.size(); ++e)
        std::sort(edge_points[e].begin(), edge_points[e].end(), EdgePointLess());

    /*
     * Index the polyline pieces by the triangle they lie in, so that the
     * triangles can be processed in linear time.
     */
    struct PieceRef {
            int quad_edge;
            int step;
    };
    std::map<int, std::vector<PieceRef> > pieces_of_tri;
    for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
        const QuadEdge &qe = _out_layout.quad_edges[q];
        for (size_t s = 0; s < qe.steps.size(); ++s) {
            PieceRef ref;
            ref.quad_edge = (int)q;
            ref.step = (int)s;
            pieces_of_tri[qe.steps[s].fh.idx()].push_back(ref);
        }
    }

    /*
     * ==================================================================
     * 3. Split every triangle, yielding the refined faces
     * ==================================================================
     */
    struct RefinedFace {
            std::vector<int> vertices;
            std::vector<int> edges;
            int tri_face;
    };
    std::vector<RefinedFace> refined_faces;

    const bool have_face_status = tm.has_face_status();

    for (typename TMesh::FaceIter f = tm.faces_begin(); f != tm.faces_end(); ++f) {
        if (have_face_status && tm.status(*f).deleted()) continue;

        HEH hehs[3];
        hehs[0] = tm.halfedge_handle(*f);
        hehs[1] = tm.next_halfedge_handle(hehs[0]);
        hehs[2] = tm.next_halfedge_handle(hehs[1]);

        Point_2 uv[3];
        for (int i = 0; i < 3; ++i) uv[i] = uv_of(hehs[i]);

        const ORIENTATION ori = Triangle_2(uv[0], uv[1], uv[2]).orientation();
        if (ori == ORI_ZERO) {
            /*
             * Degenerate in parameter space; QEx skips those triangles. We keep
             * them as a single refined face so that the refined mesh still
             * covers the whole triangle mesh, but they cannot be assigned to a
             * cell.
             */
            ++_out_layout.n_degenerate_triangles;
            RefinedFace rf;
            for (int i = 0; i < 3; ++i)
                rf.vertices.push_back(vh_to_vertex[tm.to_vertex_handle(hehs[i]).idx()]);
            rf.edges.assign(3, -1);
            rf.tri_face = f->idx();
            if (rf.vertices[0] >= 0 && rf.vertices[1] >= 0 && rf.vertices[2] >= 0)
                refined_faces.push_back(rf);
            continue;
        }

        /*
         * Boundary loop of the triangle, split at all registered points, in the
         * triangle's winding order: corner 0, the points on edge(hehs[1]),
         * corner 1, the points on edge(hehs[2]), corner 2, the points on
         * edge(hehs[0]).
         */
        std::vector<int> boundary;
        for (int i = 0; i < 3; ++i) {
            boundary.push_back(vh_to_vertex[tm.to_vertex_handle(hehs[i]).idx()]);
            const EH eh = tm.edge_handle(hehs[(i + 1) % 3]);
            std::vector<int> chain;
            const std::vector<EdgePoint> &pts = edge_points[eh.idx()];
            for (size_t k = 0; k < pts.size(); ++k) chain.push_back(pts[k].vertex);
            if (tm.halfedge_handle(eh, 0) != hehs[(i + 1) % 3])
                std::reverse(chain.begin(), chain.end());
            boundary.insert(boundary.end(), chain.begin(), chain.end());
        }

        /* drop consecutive duplicates (e.g. grid vertices registered on a corner) */
        {
            std::vector<int> cleaned;
            for (size_t k = 0; k < boundary.size(); ++k) {
                if (boundary[k] < 0) continue;
                if (!cleaned.empty() && cleaned.back() == boundary[k]) continue;
                cleaned.push_back(boundary[k]);
            }
            if (cleaned.size() > 1 && cleaned.front() == cleaned.back()) cleaned.pop_back();
            boundary.swap(cleaned);
        }

        if (boundary.size() < 3) {
            ++_out_layout.n_degenerate_triangles;
            continue;
        }

        /*
         * uv positions of the arrangement's vertices, expressed in this
         * triangle's uv frame. They are used for the angular ordering of the
         * edges around a vertex and for the orientation of the extracted faces.
         */
        std::map<int, std::pair<double, double> > positions2d;

        /* the triangle's corners */
        for (int i = 0; i < 3; ++i)
            positions2d[vh_to_vertex[tm.to_vertex_handle(hehs[i]).idx()]] =
                    std::make_pair(uv[i][0], uv[i][1]);

        /*
         * Every other vertex of the arrangement is an endpoint of polyline
         * pieces, and the uv of those endpoints in this triangle's frame was
         * recorded while tracing - so no geometric reconstruction is needed
         * (and none would be reliable: a 3d point lying on one edge of the
         * triangle also projects into the parameter range of the other edges).
         */
        const typename std::map<int, std::vector<PieceRef> >::const_iterator pieces_here =
                pieces_of_tri.find(f->idx());
        if (pieces_here != pieces_of_tri.end()) {
            for (size_t i = 0; i < pieces_here->second.size(); ++i) {
                const QuadEdge &qe = _out_layout.quad_edges[pieces_here->second[i].quad_edge];
                const int step = pieces_here->second[i].step;
                if (step + 1 >= (int)qe.step_vertices.size()) continue;
                const int a = qe.step_vertices[step], b = qe.step_vertices[step + 1];
                if (a >= 0)
                    positions2d[a] = std::make_pair(qe.steps[step].uv_in[0], qe.steps[step].uv_in[1]);
                if (b >= 0)
                    positions2d[b] = std::make_pair(qe.steps[step].uv_out[0], qe.steps[step].uv_out[1]);
            }
        }

        /* grid vertices inside this triangle: all of their local edges start here */
        const std::vector<int> &gvs_here = face_gvertices_[f->idx()];
        for (size_t k = 0; k < gvs_here.size(); ++k) {
            const int gv = gvs_here[k];
            if (gvertices_[gv].type != GridVertex::OnFace) continue;
            positions2d[gv_to_vertex[gv]] = std::make_pair(gvertices_[gv].position_uv[0],
                    gvertices_[gv].position_uv[1]);
        }

        /*
         * Whatever is left are points on the triangle's boundary without a
         * recorded uv (e.g. grid vertices on a boundary edge whose local edges
         * all start in the neighbouring triangle); place them by interpolating
         * along the edge they are closest to.
         */
        for (size_t k = 0; k < boundary.size(); ++k) {
            const int v = boundary[k];
            if (positions2d.count(v)) continue;
            int best_edge = -1;
            double best_dist = 0.0, best_local = 0.0;
            for (int i = 0; i < 3; ++i) {
                const EH eh = tm.edge_handle(hehs[i]);
                const double param = Registry::edge_param(tm, *this, eh, positions[v]);
                const double local = (tm.halfedge_handle(eh, 0) == hehs[i]) ? param : 1.0 - param;
                const double clamped = std::max(0.0, std::min(1.0, local));
                const P3 a = surface_point(tm.prev_halfedge_handle(hehs[i]));
                const P3 b = surface_point(hehs[i]);
                double dist2 = 0.0;
                for (int d = 0; d < 3; ++d) {
                    const double e = a[d] + clamped * (b[d] - a[d]) - positions[v][d];
                    dist2 += e * e;
                }
                if (best_edge < 0 || dist2 < best_dist) {
                    best_dist = dist2;
                    best_edge = i;
                    best_local = clamped;
                }
            }
            if (best_edge >= 0) {
                const Point_2 &pa = uv[(best_edge + 2) % 3], &pb = uv[best_edge];
                positions2d[v] = std::make_pair(pa[0] + best_local * (pb[0] - pa[0]),
                        pa[1] + best_local * (pb[1] - pa[1]));
            } else {
                positions2d[v] = std::make_pair(uv[0][0], uv[0][1]);
            }
        }

        /* boundary plus all polyline pieces passing through this triangle */
        LocalArrangement arrangement(boundary);
        if (pieces_here != pieces_of_tri.end()) {
            for (size_t i = 0; i < pieces_here->second.size(); ++i) {
                const int q = pieces_here->second[i].quad_edge;
                const int step = pieces_here->second[i].step;
                const QuadEdge &qe = _out_layout.quad_edges[q];
                if (step + 1 >= (int)qe.step_vertices.size()) continue;
                const int a = qe.step_vertices[step], b = qe.step_vertices[step + 1];
                if (a < 0 || b < 0) { ++_out_layout.n_unresolved_segments; continue; }
                if (a == b) {
                    /*
                     * The whole piece collapsed to a single point (e.g. a
                     * reversing edge, or two crossings that fell onto the very
                     * same spot on a triangle edge). Such a piece separates
                     * nothing, so it is not an error.
                     */
                    ++n_degenerate_pieces;
                    continue;
                }
                if (!positions2d.count(a) || !positions2d.count(b)) {
                    ++_out_layout.n_unresolved_segments;
                    continue;
                }
                arrangement.add_edge(a, b, q);
            }
        }

        _out_layout.n_pruned_pieces += arrangement.prune_leaves(&pruned_edges);

        /*
         * The outer face of the arrangement is the one that traverses the
         * triangle's boundary against the winding we built it with (see the
         * boundary loop above): for a positively oriented uv triangle that is
         * the reversed direction, for a flipped one the very same direction.
         */
        std::pair<int, int> outer_directed_edge(-1, -1);
        if (boundary.size() >= 2) {
            if (ori == ORI_POSITIVE)
                outer_directed_edge = std::make_pair(boundary[1], boundary[0]);
            else
                outer_directed_edge = std::make_pair(boundary[0], boundary[1]);
        }

        std::vector<std::vector<int> > faces, face_edges;
        arrangement.extract_faces(positions2d, outer_directed_edge, faces, face_edges);

        dropped_unclosed += arrangement.n_dropped_unclosed;
        dropped_orientation += arrangement.n_dropped_orientation;
        dropped_missing += arrangement.n_dropped_missing_position;
        (void)dropped_unclosed; (void)dropped_orientation; (void)dropped_missing;
        (void)n_degenerate_pieces;
        for (size_t k = 0; k < faces.size(); ++k) {
            RefinedFace rf;
            rf.vertices = faces[k];
            rf.edges = face_edges[k];
            rf.tri_face = f->idx();

            /*
             * Normalize the winding: the loops come out of the arrangement with
             * the triangle's uv orientation, which is reversed with respect to
             * the surface orientation iff the parametrization flips here.
             */
            if (ori == ORI_NEGATIVE) {
                std::vector<int> nv, ne;
                nv.push_back(rf.vertices.front());
                for (size_t e = rf.vertices.size(); e-- > 1;)
                    nv.push_back(rf.vertices[e]);
                ne.assign(rf.edges.rbegin(), rf.edges.rend());
                rf.vertices.swap(nv);
                rf.edges.swap(ne);
            }
            refined_faces.push_back(rf);
        }
    }

    /*
     * ==================================================================
     * 4. Flood fill the cells across all non-quad-edge edges
     * ==================================================================
     */
    std::map<std::pair<int, int>, std::vector<int> > faces_of_edge;
    for (size_t k = 0; k < refined_faces.size(); ++k) {
        const std::vector<int> &vs = refined_faces[k].vertices;
        for (size_t e = 0; e < vs.size(); ++e) {
            const int a = vs[e], b = vs[(e + 1) % vs.size()];
            if (a == b) continue;
            faces_of_edge[std::pair<int, int>(std::min(a, b), std::max(a, b))].push_back((int)k);
        }
    }

    if (layout_diagnostics_enabled()) {
        /* which quad edge chain edges did not make it into the refined mesh? */
        size_t chain_edges = 0, lost = 0, shown = 0;
        for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
            const QuadEdge &qe = _out_layout.quad_edges[q];
            size_t this_lost = 0;
            for (size_t c = 0; c + 1 < qe.step_vertices.size(); ++c) {
                const int a = qe.step_vertices[c], b = qe.step_vertices[c + 1];
                if (a < 0 || b < 0 || a == b) continue;
                ++chain_edges;
                const std::pair<int, int> key(std::min(a, b), std::max(a, b));
                if (!faces_of_edge.count(key)) {
                    ++lost; ++this_lost;
                    if (pruned_edges.count(key)) { /* pruned */ }
                    else if (dropped_face_edges_shown < 6) {
                        ++dropped_face_edges_shown;
                        std::cerr << "[lost-edge] not pruned but absent: " << a << "-" << b
                                  << " (qedge " << q << ")" << std::endl;
                    }
                }
            }
            if (this_lost && shown < 8) {
                ++shown;
                std::cerr << "[lost] qedge " << q << " gv " << qe.gv_a << "->" << qe.gv_b
                          << " chain " << qe.step_vertices.size() << " lost " << this_lost
                          << " cells " << qe.cell_left << "/" << qe.cell_right
                          << " poly_faces " << qe.poly_face_a << "/" << qe.poly_face_b << std::endl;
            }
        }
        size_t pruned_lost = 0;
        for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
            const QuadEdge &qe = _out_layout.quad_edges[q];
            for (size_t c = 0; c + 1 < qe.step_vertices.size(); ++c) {
                const int a = qe.step_vertices[c], b = qe.step_vertices[c + 1];
                if (a < 0 || b < 0 || a == b) continue;
                const std::pair<int, int> key(std::min(a, b), std::max(a, b));
                if (!faces_of_edge.count(key) && pruned_edges.count(key)) ++pruned_lost;
            }
        }
        std::cerr << "[drop-summary] unclosed " << dropped_unclosed << " orientation "
                  << dropped_orientation << " missing " << dropped_missing << std::endl;
        std::cerr << "[lost] chain edges " << chain_edges << ", not part of any refined face: "
                  << lost << " (of which pruned by leaf stripping: " << pruned_lost
                  << "), pruned edges total " << pruned_edges.size()
                  << ", barrier keys " << quad_edge_of_refined_edge.size()
                  << ", refined edges " << faces_of_edge.size() << std::endl;
    }

    UnionFind uf(refined_faces.size());
    for (std::map<std::pair<int, int>, std::vector<int> >::const_iterator it =
            faces_of_edge.begin(); it != faces_of_edge.end(); ++it) {
        if (quad_edge_of_refined_edge.count(it->first)) continue;
        for (size_t i = 1; i < it->second.size(); ++i)
            uf.unite(it->second[0], it->second[i]);
    }

    std::map<size_t, int> cell_of_root;
    std::vector<int> face_cell(refined_faces.size(), -1);
    for (size_t k = 0; k < refined_faces.size(); ++k) {
        const size_t root = uf.find(k);
        std::map<size_t, int>::iterator it = cell_of_root.find(root);
        if (it == cell_of_root.end()) {
            const int id = (int)cell_of_root.size();
            cell_of_root[root] = id;
            face_cell[k] = id;
        } else {
            face_cell[k] = it->second;
        }
    }
    int n_cells = (int)cell_of_root.size();

    /*
     * ==================================================================
     * 5. Which poly mesh face is each cell?
     *
     * A cell lies on the left of a directed quad edge (gv_a -> gv_b) iff its
     * boundary traverses the corresponding refined edge in the same direction.
     * The poly mesh stores exactly the same two sides in poly_face_a/b.
     * ==================================================================
     */
    /*
     * For every quad edge: the (refined face, direction) on either of its sides.
     * This is computed once and reused after the cell merging below.
     */
    std::map<int, std::vector<std::pair<int, bool> > > quad_edge_sides;
    for (size_t k = 0; k < refined_faces.size(); ++k) {
        const RefinedFace &rf = refined_faces[k];
        for (size_t e = 0; e < rf.vertices.size(); ++e) {
            const int q = rf.edges[e];
            if (q < 0) continue;
            const int a = rf.vertices[e], b = rf.vertices[(e + 1) % rf.vertices.size()];
            const QuadEdge &qe = _out_layout.quad_edges[q];
            /*
             * The quad edge is a chain of refined edges; this particular refined
             * edge is traversed in the quad edge's direction (gv_a -> gv_b) iff it
             * appears as a consecutive pair of the chain in that order.
             */
            bool same_direction = false;
            for (size_t c = 0; c + 1 < qe.step_vertices.size(); ++c) {
                if (qe.step_vertices[c] == a && qe.step_vertices[c + 1] == b) {
                    same_direction = true;
                    break;
                }
            }
            quad_edge_sides[q].push_back(std::make_pair((int)k, same_direction));
        }
    }

    /*
     * A quad edge can be realised along exactly the same curve as another one
     * (this happens at folds of the parametrization, where the relaxed integer
     * grid maps two logical grid edges onto the same segment). Its refined edges
     * are then owned by the first quad edge, so it has no sides of its own; count
     * them instead of treating them as broken.
     */
    std::vector<int> coincident_with(_out_layout.quad_edges.size(), -1);
    size_t n_coincident = 0;
    for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
        if (quad_edge_sides.count((int)q)) continue;
        const std::vector<int> &chain = _out_layout.quad_edges[q].step_vertices;
        bool distinct = false;
        for (size_t i = 1; i < chain.size() && !distinct; ++i)
            if (chain[i] != chain[0]) distinct = true;
        if (!distinct) continue;
        ++n_coincident;
        /* which quad edge owns this curve? the first chain edge gives it away */
        for (size_t i = 0; i + 1 < chain.size() && coincident_with[q] < 0; ++i) {
            const std::pair<int, int> key(std::min(chain[i], chain[i + 1]),
                    std::max(chain[i], chain[i + 1]));
            const std::map<std::pair<int, int>, int>::const_iterator it =
                    quad_edge_of_refined_edge.find(key);
            if (it != quad_edge_of_refined_edge.end() && it->second != (int)q)
                coincident_with[q] = it->second;
        }
    }
    _out_layout.n_coincident_quad_edges = n_coincident;

    /*
     * Derive, for every cell, the poly mesh face it belongs to: a cell lies on
     * the left of a directed quad edge (gv_a -> gv_b) iff its boundary traverses
     * the corresponding refined edge in that direction, and the poly mesh stores
     * exactly those two sides in poly_face_a / poly_face_b.
     */
    std::vector<int> cell_poly_face;
    std::vector<char> conflict;
    size_t n_conflicts = 0;
    {
        cell_poly_face.assign(n_cells, -1);
        conflict.assign(n_cells, 0);
        for (std::map<int, std::vector<std::pair<int, bool> > >::const_iterator it =
                quad_edge_sides.begin(); it != quad_edge_sides.end(); ++it) {
            const QuadEdge &qe = _out_layout.quad_edges[it->first];
            for (size_t i = 0; i < it->second.size(); ++i) {
                const int poly_face = it->second[i].second ? qe.poly_face_a : qe.poly_face_b;
                const int cell = face_cell[it->second[i].first];
                if (cell < 0 || poly_face < 0) continue;
                if (cell_poly_face[cell] < 0) cell_poly_face[cell] = poly_face;
                else if (cell_poly_face[cell] != poly_face) conflict[cell] = 1;
            }
        }
        for (int c = 0; c < n_cells; ++c)
            if (conflict[c]) ++n_conflicts;
    }

    /*
     * Merge cells that are covered by the very same poly mesh face.
     *
     * QEx's face construction accepts faces with "double edges", so a face can
     * contain a slit: the grid then splits that face's region into two cells,
     * which are both bounded by (and therefore assigned to) that one face. The
     * quad mesh's face is the unit of the correspondence the caller is
     * interested in, so those cells are merged into a single cell.
     */
    int n_cells_before_merge = n_cells;
    {
        UnionFind cell_uf(n_cells);
        std::map<int, int> first_cell_of_face;
        for (int c = 0; c < n_cells; ++c) {
            if (cell_poly_face[c] < 0) continue;
            std::map<int, int>::iterator it = first_cell_of_face.find(cell_poly_face[c]);
            if (it == first_cell_of_face.end()) first_cell_of_face[cell_poly_face[c]] = c;
            else cell_uf.unite(it->second, c);
        }
        std::map<size_t, int> new_index_of_root;
        std::vector<int> remap(n_cells, -1);
        for (int c = 0; c < n_cells; ++c) {
            const size_t root = cell_uf.find(c);
            std::map<size_t, int>::iterator it = new_index_of_root.find(root);
            if (it == new_index_of_root.end()) {
                const int id = (int)new_index_of_root.size();
                new_index_of_root[root] = id;
                remap[c] = id;
            } else {
                remap[c] = it->second;
            }
        }
        if ((int)new_index_of_root.size() != n_cells) {
            for (size_t k = 0; k < face_cell.size(); ++k)
                if (face_cell[k] >= 0) face_cell[k] = remap[face_cell[k]];
            n_cells = (int)new_index_of_root.size();
            /* the face of a merged cell: any of the merged cells' faces */
            std::vector<int> merged_face(n_cells, -1);
            for (int c = 0; c < (int)remap.size(); ++c)
                if (remap[c] >= 0 && cell_poly_face[c] >= 0) merged_face[remap[c]] = cell_poly_face[c];
            cell_poly_face.assign(n_cells, -1);
            conflict.assign(n_cells, 0);
            for (std::map<int, std::vector<std::pair<int, bool> > >::const_iterator it =
                    quad_edge_sides.begin(); it != quad_edge_sides.end(); ++it) {
                const QuadEdge &qe = _out_layout.quad_edges[it->first];
                for (size_t i = 0; i < it->second.size(); ++i) {
                    const int poly_face = it->second[i].second ? qe.poly_face_a : qe.poly_face_b;
                    const int cell = face_cell[it->second[i].first];
                    if (cell < 0 || poly_face < 0) continue;
                    if (cell_poly_face[cell] < 0) cell_poly_face[cell] = poly_face;
                    else if (cell_poly_face[cell] != poly_face) conflict[cell] = 1;
                }
            }
            n_conflicts = 0;
            for (int c = 0; c < n_cells; ++c) {
                if (conflict[c]) ++n_conflicts;
                if (cell_poly_face[c] < 0) cell_poly_face[c] = merged_face[c];
            }
        }
    }
    _out_layout.n_cell_poly_face_conflicts = n_conflicts;

    /*
     * Cells whose boundary quad edges carry no poly mesh halfedge are regions
     * that QEx's face construction did not turn into a face (these are the
     * "undesired holes" the extractor reports). They are left unassigned
     * (cell_poly_face == -1) instead of being matched to some face by guesswork.
     */
    size_t n_cells_without_face = 0;
    for (int c = 0; c < n_cells; ++c)
        if (cell_poly_face[c] < 0) ++n_cells_without_face;
    _out_layout.n_cells_without_poly_face = n_cells_without_face;

    {
        std::vector<char> covered(_poly_mesh.n_faces(), 0);
        for (int c = 0; c < n_cells; ++c)
            if (cell_poly_face[c] >= 0 && cell_poly_face[c] < (int)covered.size())
                covered[cell_poly_face[c]] = 1;
        size_t uncovered = 0;
        for (size_t i = 0; i < covered.size(); ++i) if (!covered[i]) ++uncovered;
        _out_layout.n_poly_faces_without_cell = uncovered;
    }

    /*
     * ==================================================================
     * 6. Assemble the layout
     * ==================================================================
     */
    _out_layout.n_degenerate_pieces = n_degenerate_pieces;
    {
        size_t collapsed = 0;
        for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
            size_t distinct = 0;
            const std::vector<int> &chain = _out_layout.quad_edges[q].step_vertices;
            for (size_t i = 0; i < chain.size(); ++i) {
                bool seen = false;
                for (size_t j = 0; j < i; ++j) if (chain[j] == chain[i]) { seen = true; break; }
                if (!seen) ++distinct;
            }
            if (distinct < 2) ++collapsed;
        }
        _out_layout.n_collapsed_quad_edges = collapsed;
    }
    _out_layout.n_desired_holes = n_desired_holes_;
    _out_layout.n_undesired_holes = n_undesired_holes_;
    _out_layout.n_poly_faces = (int)_poly_mesh.n_faces();
    _out_layout.n_cells = n_cells;
    _out_layout.cell_poly_face = cell_poly_face;
    _out_layout.vertex_position = positions;
    _out_layout.vertex_kind = kind;
    _out_layout.vertex_tri_vertex = tri_vertex;
    _out_layout.vertex_tri_edge = tri_edge;
    _out_layout.vertex_grid_vertex = grid_vertex;

    _out_layout.face_vertices.resize(refined_faces.size());
    _out_layout.face_edge_quad_edge.resize(refined_faces.size());
    _out_layout.face_tri_face.resize(refined_faces.size());
    _out_layout.face_cell.resize(refined_faces.size());
    for (size_t k = 0; k < refined_faces.size(); ++k) {
        _out_layout.face_vertices[k] = refined_faces[k].vertices;
        _out_layout.face_edge_quad_edge[k] = refined_faces[k].edges;
        _out_layout.face_tri_face[k] = refined_faces[k].tri_face;
        _out_layout.face_cell[k] = face_cell[k];
    }

    for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
        QuadEdge &qe = _out_layout.quad_edges[q];
        qe.cell_left = -1;
        qe.cell_right = -1;
        const std::map<int, std::vector<std::pair<int, bool> > >::const_iterator it =
                quad_edge_sides.find((int)q);
        if (it == quad_edge_sides.end()) continue;
        for (size_t i = 0; i < it->second.size(); ++i) {
            const int cell = face_cell[it->second[i].first];
            if (cell < 0) continue;
            if (it->second[i].second) qe.cell_left = cell;
            else qe.cell_right = cell;
        }
    }

    /* coincident quad edges share the cells of the curve they run along */
    for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
        QuadEdge &qe = _out_layout.quad_edges[q];
        if (qe.cell_left >= 0 || qe.cell_right >= 0) continue;
        if (coincident_with[q] < 0) continue;
        const QuadEdge &owner = _out_layout.quad_edges[coincident_with[q]];
        qe.cell_left = owner.cell_left;
        qe.cell_right = owner.cell_right;
    }

    _out_layout.cell_quad_edges.assign(n_cells, std::vector<int>());
    _out_layout.cell_tri_faces.assign(n_cells, std::vector<int>());
    _out_layout.cell_vertices.assign(n_cells, std::vector<int>());

    for (size_t q = 0; q < _out_layout.quad_edges.size(); ++q) {
        const QuadEdge &qe = _out_layout.quad_edges[q];
        if (qe.cell_left >= 0)
            _out_layout.cell_quad_edges[qe.cell_left].push_back((int)q);
        if (qe.cell_right >= 0 && qe.cell_right != qe.cell_left)
            _out_layout.cell_quad_edges[qe.cell_right].push_back((int)q);
    }

    for (size_t k = 0; k < refined_faces.size(); ++k) {
        const int cell = face_cell[k];
        if (cell < 0) continue;
        std::vector<int> &tv = _out_layout.cell_tri_faces[cell];
        if (std::find(tv.begin(), tv.end(), refined_faces[k].tri_face) == tv.end())
            tv.push_back(refined_faces[k].tri_face);
    }

    /* stitch the boundary of every cell from its quad edge chains */
    for (int c = 0; c < n_cells; ++c) {
        std::map<int, std::vector<int> > chains;
        for (size_t i = 0; i < _out_layout.cell_quad_edges[c].size(); ++i) {
            const QuadEdge &qe = _out_layout.quad_edges[_out_layout.cell_quad_edges[c][i]];
            std::vector<int> chain = qe.step_vertices;
            if (chain.size() < 2) continue;
            if (qe.cell_left != c) std::reverse(chain.begin(), chain.end());
            chains[chain.front()] = chain;
        }
        if (chains.empty()) continue;
        std::vector<int> loop;
        const int start = chains.begin()->first;
        int current = start;
        for (size_t guard = 0; guard <= chains.size(); ++guard) {
            std::map<int, std::vector<int> >::iterator it = chains.find(current);
            if (it == chains.end()) break;
            const std::vector<int> chain = it->second;
            chains.erase(it);
            for (size_t k = 0; k + 1 < chain.size(); ++k) loop.push_back(chain[k]);
            current = chain.back();
            if (current == start) break;
        }
        _out_layout.cell_vertices[c] = loop;
    }

    if (layout_diagnostics_enabled()) {
        std::map<int, int> cells_per_face;
        for (int c = 0; c < n_cells; ++c)
            if (cell_poly_face[c] >= 0) ++cells_per_face[cell_poly_face[c]];
        std::map<int, int> hist;
        for (std::map<int, int>::const_iterator it = cells_per_face.begin();
                it != cells_per_face.end(); ++it)
            ++hist[it->second];
        std::cerr << "[diag] poly faces: " << _poly_mesh.n_faces() << ", covered by a cell: "
                  << cells_per_face.size() << "; cells per face histogram:";
        for (std::map<int, int>::const_iterator it = hist.begin(); it != hist.end(); ++it)
            std::cerr << " " << it->first << "cell->" << it->second << "faces";
        std::cerr << std::endl;
        std::cerr << "[diag] cells before merging by poly face: " << n_cells_before_merge
                  << ", after: " << n_cells << std::endl;
        std::cerr << "[diag] cells: " << n_cells << ", cells without poly face: ";
        size_t no_face = 0;
        for (int c = 0; c < n_cells; ++c) if (cell_poly_face[c] < 0) ++no_face;
        std::cerr << no_face << ", degenerate pieces (collapsed to a point): "
                  << n_degenerate_pieces << ", degenerate quad edges (fins): "
                  << _out_layout.n_degenerate_quad_edges << std::endl;

        size_t shown_conflicts = 0;
        for (int c = 0; c < n_cells && shown_conflicts < 6; ++c) {
            if (!conflict[c]) continue;
            ++shown_conflicts;
            std::cerr << "[conflict] cell " << c << " cell_poly_face " << cell_poly_face[c]
                      << " quad edges " << _out_layout.cell_quad_edges[c].size() << ":";
            for (size_t i = 0; i < _out_layout.cell_quad_edges[c].size(); ++i) {
                const int q = _out_layout.cell_quad_edges[c][i];
                const QuadEdge &qe = _out_layout.quad_edges[q];
                std::cerr << " [q" << q << " left " << qe.cell_left << " right " << qe.cell_right
                          << " faces " << qe.poly_face_a << "/" << qe.poly_face_b << "]";
            }
            std::cerr << std::endl;
        }

        std::cerr << "[diag] poly faces without a cell: " << _out_layout.n_poly_faces_without_cell
                  << ", cells without a poly face: " << n_cells_without_face << std::endl;
        {
            std::vector<char> covered(_poly_mesh.n_faces(), 0);
            for (int c = 0; c < n_cells; ++c)
                if (cell_poly_face[c] >= 0 && cell_poly_face[c] < (int)covered.size())
                    covered[cell_poly_face[c]] = 1;
            size_t shown = 0;
            for (size_t i = 0; i < covered.size() && shown < 6; ++i) {
                if (covered[i]) continue;
                ++shown;
                std::cerr << "[uncovered-face] poly face " << i << " valence "
                          << _poly_mesh.valence(_poly_mesh.face_handle((int)i))
                          << " halfedges:";
                for (typename PolyMeshT::ConstFaceHalfedgeIter fh =
                        _poly_mesh.cfh_begin(_poly_mesh.face_handle((int)i));
                        fh.is_valid(); ++fh) {
                    const int q = -1;
                    (void)q;
                    std::cerr << " v" << _poly_mesh.to_vertex_handle(*fh).idx();
                }
                std::cerr << std::endl;
            }
        }

        size_t shown_multi = 0;
        for (std::map<int, int>::const_iterator it = cells_per_face.begin();
                it != cells_per_face.end() && shown_multi < 6; ++it) {
            if (it->second < 2) continue;
            ++shown_multi;
            std::cerr << "[multi-cell] poly face " << it->first << " is covered by " << it->second
                      << " cells:";
            for (int c = 0; c < n_cells; ++c)
                if (cell_poly_face[c] == it->first) std::cerr << " " << c;
            std::cerr << std::endl;
        }
    }

    /*
     * ==================================================================
     * 7. Write the refined mesh
     *
     * Only well formed, manifold faces are handed to OpenMesh: a face with
     * repeated vertices or a face that would put a third face onto one of its
     * edges would make add_face() fail (and may make it loop), so we check that
     * ourselves and report the offenders instead.
     * ==================================================================
     */
    std::vector<typename LayoutMeshT::VertexHandle> vhs(positions.size());
    for (size_t i = 0; i < positions.size(); ++i)
        vhs[i] = _out_refined_mesh.add_vertex(typename LayoutMeshT::Point(
                positions[i][0], positions[i][1], positions[i][2]));

    std::map<std::pair<int, int>, int> faces_on_edge;
    for (size_t k = 0; k < refined_faces.size(); ++k) {
        const std::vector<int> &vs = refined_faces[k].vertices;

        bool skip = vs.size() < 3;
        const char *reason = vs.size() < 3 ? "few vertices" : 0;
        for (size_t e = 0; e < vs.size() && !skip; ++e)
            for (size_t g = e + 1; g < vs.size(); ++g)
                if (vs[e] == vs[g]) { skip = true; reason = "repeated vertex"; break; }

        for (size_t e = 0; e < vs.size() && !skip; ++e) {
            const int a = vs[e], b = vs[(e + 1) % vs.size()];
            if (a == b) { skip = true; reason = "degenerate edge"; break; }
            if (faces_on_edge[std::pair<int, int>(std::min(a, b), std::max(a, b))] >= 2) {
                skip = true;
                reason = "third face on edge";
                break;
            }
        }

        if (skip) {
            ++_out_layout.n_failed_refined_faces;
            continue;
        }

        for (size_t e = 0; e < vs.size(); ++e) {
            const int a = vs[e], b = vs[(e + 1) % vs.size()];
            ++faces_on_edge[std::pair<int, int>(std::min(a, b), std::max(a, b))];
        }

        std::vector<typename LayoutMeshT::VertexHandle> face;
        face.reserve(vs.size());
        for (size_t e = 0; e < vs.size(); ++e)
            face.push_back(vhs[vs[e]]);
        if (!_out_refined_mesh.add_face(face).is_valid())
            ++_out_layout.n_failed_refined_faces;
    }
}

} // namespace QEx
