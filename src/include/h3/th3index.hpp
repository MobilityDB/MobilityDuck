#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* H3INDEX is a 64-bit unsigned cell id; surfaced as BIGINT (signed
 * reinterpretation is safe because the comparison and equality
 * operators care only about the bit pattern).  TH3INDEX is the
 * temporal cell index, stored as a Temporal* blob (BLOB).  */
struct H3IndexTypes {
    static LogicalType H3INDEX();
    static LogicalType TH3INDEX();
    /* H3INDEXSET is a Set<H3INDEX>, stored as a serialized Set* blob,
     * built from a static geometry by `geoToH3IndexSet`.  Used as the
     * static side of the trip×static h3 prefilter on Q4 / Q7 / Q11 /
     * Q12 / Q15 / Q17. */
    static LogicalType H3INDEXSET();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

struct H3IndexFunctions {
    /* In/out — H3 cell scalar */
    static bool H3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool H3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void H3index_from_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void H3index_as_text(DataChunk &args, ExpressionState &state, Vector &result);

    /* In/out — TH3INDEX temporal value */
    static bool Th3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Th3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* Constructor */
    static void Th3index_make(DataChunk &args, ExpressionState &state, Vector &result);

    /* Accessors */
    static void Th3index_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_value_n(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);

    /* Casts to/from other temporal types */
    static void Tbigint_to_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_to_tbigint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeogpoint_to_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeompoint_to_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_to_tgeogpoint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_to_tgeompoint(DataChunk &args, ExpressionState &state, Vector &result);

    /* Ever / always boolean predicates */
    static void Ever_eq_h3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_th3index_h3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_th3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_h3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_th3index_h3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_th3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_h3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_th3index_h3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_th3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_h3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_th3index_h3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_th3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);

    /* Temporal equality / inequality (returns tbool) */
    static void Teq_h3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Teq_th3index_h3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Teq_th3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tne_h3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tne_th3index_h3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tne_th3index_th3index(DataChunk &args, ExpressionState &state, Vector &result);

    /* H3 cell properties — all `Temporal *fn(const Temporal *)` */
    static void Th3index_get_resolution(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_get_base_cell_number(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_is_valid_cell(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_is_res_class_iii(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_is_pentagon(DataChunk &args, ExpressionState &state, Vector &result);

    /* Hierarchy */
    static void Th3index_cell_to_parent(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_cell_to_parent_next(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_cell_to_center_child(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_cell_to_center_child_next(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_cell_to_child_pos(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_child_pos_to_cell(DataChunk &args, ExpressionState &state, Vector &result);

    /* Geometry / boundary */
    static void Th3index_cell_to_boundary(DataChunk &args, ExpressionState &state, Vector &result);

    /* Directed edges */
    static void Th3index_are_neighbor_cells(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_cells_to_directed_edge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_is_valid_directed_edge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_get_directed_edge_origin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_get_directed_edge_destination(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_directed_edge_to_boundary(DataChunk &args, ExpressionState &state, Vector &result);

    /* Vertices */
    static void Th3index_cell_to_vertex(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_vertex_to_latlng(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_is_valid_vertex(DataChunk &args, ExpressionState &state, Vector &result);

    /* Grid traversal */
    static void Th3index_grid_distance(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_cell_to_local_ij(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_local_ij_to_cell(DataChunk &args, ExpressionState &state, Vector &result);

    /* Cell area / edge length / great-circle distance */
    static void Th3index_cell_area(DataChunk &args, ExpressionState &state, Vector &result);
    static void Th3index_edge_length(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeogpoint_great_circle_distance(DataChunk &args, ExpressionState &state, Vector &result);

    /* Static geometry → h3indexset (Set<H3INDEX>) at a given H3 resolution */
    static void Geo_to_h3index_set(DataChunk &args, ExpressionState &state, Vector &result);

    /* Trip × static h3indexset prefilter (everEq): true if the th3index
     * trajectory ever takes a cell that equals a cell of the static set.
     * Registered as the everEq comparison overload in both argument
     * directions (h3indexset,th3index) and (th3index,h3indexset). */
    static void Ever_eq_h3indexset_th3index(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_th3index_h3indexset(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
