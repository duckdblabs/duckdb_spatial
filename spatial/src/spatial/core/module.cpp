
#include "spatial/core/module.hpp"

#include "spatial/common.hpp"
#include "spatial/core/functions/aggregate.hpp"
#include "spatial/core/functions/cast.hpp"
#include "spatial/core/functions/macros.hpp"
#include "spatial/core/functions/scalar.hpp"
#include "spatial/core/functions/table.hpp"
#include "spatial/core/geoarrow.hpp"
#include "spatial/core/index/rtree/rtree_module.hpp"
#include "spatial/core/optimizer_rules.hpp"
#include "spatial/core/types.hpp"

namespace spatial {

namespace core {

void CoreModule::Register(DatabaseInstance &db) {
	GeoTypes::Register(db);
	CoreScalarFunctions::Register(db);
	CoreCastFunctions::Register(db);
	CoreTableFunctions::Register(db);
	CoreAggregateFunctions::Register(db);
	CoreScalarMacros::Register(db);

	// RTree index
	RTreeModule::RegisterIndex(db);
	RTreeModule::RegisterIndexScan(db);
	RTreeModule::RegisterIndexPlanCreate(db);
	RTreeModule::RegisterIndexPlanScan(db);
	RTreeModule::RegisterIndexPragmas(db);

	// Register the optimizer extensions
	CoreOptimizerRules::Register(db);

	// Arrow extensions
	GeoArrow::Register(db);
}

} // namespace core

} // namespace spatial