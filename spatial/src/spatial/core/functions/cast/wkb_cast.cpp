#include "spatial/common.hpp"
#include "spatial/core/types.hpp"
#include "spatial/core/functions/cast.hpp"
#include "spatial/core/functions/common.hpp"
#include "spatial/core/geometry/wkb_writer.hpp"
#include "spatial/core/geometry/wkb_reader.hpp"

#include "duckdb/function/cast/cast_function_set.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/common/operator/cast_operators.hpp"

namespace spatial {

namespace core {

//------------------------------------------------------------------------------
// WKB -> GEOMETRY
//------------------------------------------------------------------------------
bool WKBToGeometryCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {

	auto &lstate = GeometryFunctionLocalState::ResetAndGet(parameters);
	WKBReader reader(lstate.arena);

	bool success = true;
	UnaryExecutor::ExecuteWithNulls<string_t, geometry_t>(
	    source, result, count, [&](string_t input, ValidityMask &mask, idx_t idx) {
		    try {
			    auto geom = reader.Deserialize(input);
			    return Geometry::Serialize(geom, result);
		    } catch (SerializationException &e) {
			    if (success) {
				    success = false;
				    ErrorData error(e);
				    HandleCastError::AssignError(error.RawMessage(), parameters.error_message);
			    }
			    mask.SetInvalid(idx);
			    return geometry_t {};
		    }
	    });
	return success;
}

//------------------------------------------------------------------------------
// GEOMETRY -> WKB
//------------------------------------------------------------------------------
bool GeometryToWKBCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {

	UnaryExecutor::Execute<geometry_t, string_t>(source, result, count,
	                                             [&](geometry_t input) { return WKBWriter::Write(input, result); });

	return true;
}

//------------------------------------------------------------------------------
//  Register functions
//------------------------------------------------------------------------------
void CoreCastFunctions::RegisterWKBCasts(DatabaseInstance &db) {
	// Geometry <-> WKB is explicitly castable
	ExtensionUtil::RegisterCastFunction(db, GeoTypes::GEOMETRY(), GeoTypes::WKB_BLOB(),
	                                    BoundCastInfo(GeometryToWKBCast));

	ExtensionUtil::RegisterCastFunction(
	    db, GeoTypes::WKB_BLOB(), GeoTypes::GEOMETRY(),
	    BoundCastInfo(WKBToGeometryCast, nullptr, GeometryFunctionLocalState::InitCast));

	// WKB -> BLOB is implicitly castable
	ExtensionUtil::RegisterCastFunction(db, GeoTypes::WKB_BLOB(), LogicalType::BLOB, DefaultCasts::ReinterpretCast, 1);

	// Geometry -> BLOB is explicitly castable
	ExtensionUtil::RegisterCastFunction(db, GeoTypes::GEOMETRY(), LogicalType::BLOB, DefaultCasts::ReinterpretCast);
}

} // namespace core

} // namespace spatial