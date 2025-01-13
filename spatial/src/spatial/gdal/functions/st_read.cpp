#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/planner/filter/conjunction_filter.hpp"
#include "duckdb/planner/filter/constant_filter.hpp"
#include "duckdb/planner/table_filter.hpp"
#include "duckdb/parser/tableref.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/replacement_scan.hpp"

#include "spatial/common.hpp"
#include "spatial/core/types.hpp"
#include "spatial/gdal/functions.hpp"
#include "spatial/gdal/file_handler.hpp"
#include "spatial/core/geometry/geometry_writer.hpp"
#include "spatial/core/geometry/wkb_reader.hpp"

#include "ogrsf_frmts.h"

namespace spatial {

namespace gdal {

enum SpatialFilterType { Wkb, Rectangle };

struct SpatialFilter {
	SpatialFilterType type;
	explicit SpatialFilter(SpatialFilterType type_p) : type(type_p) {};
};

struct RectangleSpatialFilter : SpatialFilter {
	double min_x, min_y, max_x, max_y;
	RectangleSpatialFilter(double min_x_p, double min_y_p, double max_x_p, double max_y_p)
	    : SpatialFilter(SpatialFilterType::Rectangle), min_x(min_x_p), min_y(min_y_p), max_x(max_x_p), max_y(max_y_p) {
	}
};

struct WKBSpatialFilter : SpatialFilter {
	OGRGeometryH geom;
	explicit WKBSpatialFilter(const string &wkb_p) : SpatialFilter(SpatialFilterType::Wkb), geom(nullptr) {
		auto ok = OGR_G_CreateFromWkb(wkb_p.c_str(), nullptr, &geom, (int)wkb_p.size());
		if (ok != OGRERR_NONE) {
			throw InvalidInputException("WKBSpatialFilter: could not create geometry from WKB");
		}
	}
	~WKBSpatialFilter() {
		OGR_G_DestroyGeometry(geom);
	}
};

static void TryApplySpatialFilter(OGRLayer *layer, SpatialFilter *spatial_filter) {
	if (spatial_filter != nullptr) {
		if (spatial_filter->type == SpatialFilterType::Rectangle) {
			auto &rect = (RectangleSpatialFilter &)*spatial_filter;
			layer->SetSpatialFilterRect(rect.min_x, rect.min_y, rect.max_x, rect.max_y);
		} else if (spatial_filter->type == SpatialFilterType::Wkb) {
			auto &filter = (WKBSpatialFilter &)*spatial_filter;
			layer->SetSpatialFilter(OGRGeometry::FromHandle(filter.geom));
		}
	}
}

struct GdalScanFunctionData : public TableFunctionData {
	int layer_idx;
	bool sequential_layer_scan = false;
	bool keep_wkb = false;
	unordered_set<idx_t> geometry_column_ids;
	unique_ptr<SpatialFilter> spatial_filter;
	idx_t max_threads;
	// before they are renamed
	vector<string> all_names;
	vector<LogicalType> all_types;
	ArrowTableType arrow_table;

	bool has_approximate_feature_count;
	idx_t approximate_feature_count;
	string raw_file_name;
	string prefixed_file_name;
	CPLStringList dataset_open_options;
	CPLStringList dataset_allowed_drivers;
	CPLStringList dataset_sibling_files;
	CPLStringList layer_creation_options;
};

struct GdalScanLocalState : ArrowScanLocalState {
	ArenaAllocator arena;
	// We trust GDAL to produce valid WKB
	core::WKBReader wkb_reader;
	explicit GdalScanLocalState(unique_ptr<ArrowArrayWrapper> current_chunk, ClientContext &context)
	    : ArrowScanLocalState(std::move(current_chunk), context), arena(BufferAllocator::Get(context)), wkb_reader(arena) {
	}
};

struct GdalScanGlobalState : ArrowScanGlobalState {
	GDALDatasetUniquePtr dataset;
	atomic<idx_t> lines_read;
	explicit GdalScanGlobalState(GDALDatasetUniquePtr dataset) : dataset(std::move(dataset)), lines_read(0) {
	}
};

//------------------------------------------------------------------------------
// Bind
//------------------------------------------------------------------------------
unique_ptr<FunctionData> GdalTableFunction::Bind(ClientContext &context, TableFunctionBindInput &input,
                                                 vector<LogicalType> &return_types, vector<string> &names) {

	auto result = make_uniq<GdalScanFunctionData>();

	// First scan for "options" parameter
	// auto gdal_open_options = vector<char const *>();
	auto options_param = input.named_parameters.find("open_options");
	if (options_param != input.named_parameters.end()) {
		for (auto &param : ListValue::GetChildren(options_param->second)) {
			result->dataset_open_options.AddString(StringValue::Get(param).c_str());
		}
	}

	auto drivers_param = input.named_parameters.find("allowed_drivers");
	if (drivers_param != input.named_parameters.end()) {
		for (auto &param : ListValue::GetChildren(drivers_param->second)) {
			result->dataset_allowed_drivers.AddString(StringValue::Get(param).c_str());
		}
	}

	// Now we can open the dataset
	auto &ctx_state = GDALClientContextState::GetOrCreate(context);

	auto siblings_params = input.named_parameters.find("sibling_files");
	if (siblings_params != input.named_parameters.end()) {
		for (auto &param : ListValue::GetChildren(siblings_params->second)) {
			result->dataset_sibling_files.AddString(ctx_state.GetPrefix(StringValue::Get(param)).c_str());
		}
	}

	result->raw_file_name = input.inputs[0].GetValue<string>();
	result->prefixed_file_name = ctx_state.GetPrefix(result->raw_file_name);

	auto dataset = GDALDatasetUniquePtr(GDALDataset::Open(
	    result->prefixed_file_name.c_str(), GDAL_OF_VECTOR | GDAL_OF_VERBOSE_ERROR, result->dataset_allowed_drivers,
	    result->dataset_open_options, result->dataset_sibling_files));

	if (dataset == nullptr) {
		auto error = string(CPLGetLastErrorMsg());
		throw IOException("Could not open file: " + result->raw_file_name + " (" + error + ")");
	}

	// Double check that the dataset have any layers
	if (dataset->GetLayerCount() <= 0) {
		throw IOException("Dataset does not contain any layers");
	}

	// Now we can bind the additonal options
	bool max_batch_size_set = false;
	for (auto &kv : input.named_parameters) {
		auto loption = StringUtil::Lower(kv.first);
		if (loption == "layer") {

			// Find layer by index
			if (kv.second.type() == LogicalType::INTEGER) {
				auto layer_idx = IntegerValue::Get(kv.second);
				if (layer_idx < 0) {
					throw BinderException("Layer index must be positive");
				}
				if (layer_idx > dataset->GetLayerCount()) {
					throw BinderException(
					    StringUtil::Format("Layer index too large (%s > %s)", layer_idx, dataset->GetLayerCount()));
				}
				result->layer_idx = layer_idx;
			}

			// Find layer by name
			if (kv.second.type() == LogicalTypeId::VARCHAR) {
				auto name = StringValue::Get(kv.second).c_str();
				bool found = false;
				for (auto layer_idx = 0; layer_idx < dataset->GetLayerCount(); layer_idx++) {
					if (strcmp(dataset->GetLayer(layer_idx)->GetName(), name) == 0) {
						result->layer_idx = layer_idx;
						found = true;
						break;
					}
				}
				if (!found) {
					throw BinderException(StringUtil::Format("Layer '%s' could not be found in dataset", name));
				}
			}
		}

		if (loption == "spatial_filter_box" && kv.second.type() == core::GeoTypes::BOX_2D()) {
			if (result->spatial_filter) {
				throw BinderException("Only one spatial filter can be specified");
			}
			auto &children = StructValue::GetChildren(kv.second);
			auto minx = DoubleValue::Get(children[0]);
			auto miny = DoubleValue::Get(children[1]);
			auto maxx = DoubleValue::Get(children[2]);
			auto maxy = DoubleValue::Get(children[3]);
			result->spatial_filter = make_uniq<RectangleSpatialFilter>(minx, miny, maxx, maxy);
		}

		if (loption == "spatial_filter" && kv.second.type() == core::GeoTypes::WKB_BLOB()) {
			if (result->spatial_filter) {
				throw BinderException("Only one spatial filter can be specified");
			}
			auto wkb = StringValue::Get(kv.second);
			result->spatial_filter = make_uniq<WKBSpatialFilter>(wkb);
		}

		if (loption == "max_threads") {
			auto max_threads = IntegerValue::Get(kv.second);
			if (max_threads <= 0) {
				throw BinderException("'max_threads' parameter must be positive");
			}
			result->max_threads = (idx_t)max_threads;
		}

		if (loption == "sequential_layer_scan") {
			result->sequential_layer_scan = BooleanValue::Get(kv.second);
		}

		if (loption == "max_batch_size") {
			auto max_batch_size = IntegerValue::Get(kv.second);
			if (max_batch_size <= 0) {
				throw BinderException("'max_batch_size' parameter must be positive");
			}
			auto str = StringUtil::Format("MAX_FEATURES_IN_BATCH=%d", max_batch_size);
			result->layer_creation_options.AddString(str.c_str());
			max_batch_size_set = true;
		}

		if (loption == "keep_wkb") {
			result->keep_wkb = BooleanValue::Get(kv.second);
		}
	}

	// set default max_threads
	if (result->max_threads == 0) {
		result->max_threads = context.db->NumberOfThreads();
	}

	// Defaults
	result->layer_creation_options.AddString("INCLUDE_FID=NO");
	if (!max_batch_size_set) {
		// Set default max batch size to standard vector size
		auto str = StringUtil::Format("MAX_FEATURES_IN_BATCH=%d", STANDARD_VECTOR_SIZE);
		result->layer_creation_options.AddString(str.c_str());
	}

	// Get the schema for the selected layer
	auto layer = dataset->GetLayer(result->layer_idx);

	TryApplySpatialFilter(layer, result->spatial_filter.get());

	// Check if we can get an approximate feature count
	result->approximate_feature_count = 0;
	result->has_approximate_feature_count = false;
	if (!result->sequential_layer_scan) {
		// Dont force compute the count if its expensive
		auto count = layer->GetFeatureCount(false);
		if (count > -1) {
			result->approximate_feature_count = count;
			result->has_approximate_feature_count = true;
		}
	}

	struct ArrowArrayStream stream;
	if (!layer->GetArrowStream(&stream, result->layer_creation_options)) {
		// layer is owned by GDAL, we do not need to destory it
		throw IOException("Could not get arrow stream from layer");
	}

	struct ArrowSchema schema;
	if (stream.get_schema(&stream, &schema) != 0) {
		if (stream.release) {
			stream.release(&stream);
		}
		throw IOException("Could not get arrow schema from layer");
	}

	// The Arrow API will return attributes in this order
	// 1. FID column
	// 2. all ogr field attributes
	// 3. all geometry columns

	auto attribute_count = schema.n_children;
	auto attributes = schema.children;

	result->all_names.reserve(attribute_count + 1);
	names.reserve(attribute_count + 1);

	for (idx_t col_idx = 0; col_idx < (idx_t)attribute_count; col_idx++) {
		auto &attribute = *attributes[col_idx];

		const char ogc_flag[] = {'\x01', '\0', '\0', '\0', '\x14', '\0', '\0', '\0', 'A', 'R', 'R', 'O', 'W',
		                         ':',    'e',  'x',  't',  'e',    'n',  's',  'i',  'o', 'n', ':', 'n', 'a',
		                         'm',    'e',  '\a', '\0', '\0',   '\0', 'o',  'g',  'c', '.', 'w', 'k', 'b'};

		auto arrow_type = ArrowType::GetArrowLogicalType(DBConfig::GetConfig(context), attribute);

		auto column_name = string(attribute.name);
		auto duckdb_type = arrow_type->GetDuckType();

		if (duckdb_type.id() == LogicalTypeId::BLOB && attribute.metadata != nullptr &&
		    strncmp(attribute.metadata, ogc_flag, sizeof(ogc_flag)) == 0) {
			// This is a WKB geometry blob
			result->arrow_table.AddColumn(col_idx, std::move(arrow_type));

			if (result->keep_wkb) {
				return_types.emplace_back(core::GeoTypes::WKB_BLOB());
			} else {
				return_types.emplace_back(core::GeoTypes::GEOMETRY());
				if (column_name == "wkb_geometry") {
					column_name = "geom";
				}
			}
			result->geometry_column_ids.insert(col_idx);

		} else if (attribute.dictionary) {
			auto dictionary_type = ArrowType::GetArrowLogicalType(DBConfig::GetConfig(context), attribute);
			return_types.emplace_back(dictionary_type->GetDuckType());
			arrow_type->SetDictionary(std::move(dictionary_type));
			result->arrow_table.AddColumn(col_idx, std::move(arrow_type));
		} else {
			return_types.emplace_back(arrow_type->GetDuckType());
			result->arrow_table.AddColumn(col_idx, std::move(arrow_type));
		}

		// keep these around for projection/filter pushdown later
		// does GDAL even allow duplicate/missing names?
		result->all_names.push_back(column_name);

		if (column_name.empty()) {
			names.push_back("v" + to_string(col_idx));
		} else {
			names.push_back(column_name);
		}
	}

	schema.release(&schema);
	stream.release(&stream);

	GdalTableFunction::RenameColumns(names);

	result->all_types = return_types;

	return std::move(result);
}

void GdalTableFunction::RenameColumns(vector<string> &names) {
	unordered_map<string, idx_t> name_map;
	for (auto &column_name : names) {
		// put it all lower_case
		auto low_column_name = StringUtil::Lower(column_name);
		if (name_map.find(low_column_name) == name_map.end()) {
			// Name does not exist yet
			name_map[low_column_name]++;
		} else {
			// Name already exists, we add _x where x is the repetition number
			string new_column_name = column_name + "_" + std::to_string(name_map[low_column_name]);
			auto new_column_name_low = StringUtil::Lower(new_column_name);
			while (name_map.find(new_column_name_low) != name_map.end()) {
				// This name is already here due to a previous definition
				name_map[low_column_name]++;
				new_column_name = column_name + "_" + std::to_string(name_map[low_column_name]);
				new_column_name_low = StringUtil::Lower(new_column_name);
			}
			column_name = new_column_name;
			name_map[new_column_name_low]++;
		}
	}
}

idx_t GdalTableFunction::MaxThreads(ClientContext &context, const FunctionData *bind_data_p) {
	auto &data = bind_data_p->Cast<GdalScanFunctionData>();
	return data.max_threads;
}

//-----------------------------------------------------------------------------
// Init global
//-----------------------------------------------------------------------------
unique_ptr<GlobalTableFunctionState> GdalTableFunction::InitGlobal(ClientContext &context,
                                                                   TableFunctionInitInput &input) {
	auto &data = input.bind_data->Cast<GdalScanFunctionData>();

	auto dataset = GDALDatasetUniquePtr(
	    GDALDataset::Open(data.prefixed_file_name.c_str(), GDAL_OF_VECTOR | GDAL_OF_VERBOSE_ERROR | GDAL_OF_READONLY,
	                      data.dataset_allowed_drivers, data.dataset_open_options, data.dataset_sibling_files));
	if (dataset == nullptr) {
		auto error = string(CPLGetLastErrorMsg());
		throw IOException("Could not open file: " + data.raw_file_name + " (" + error + ")");
	}

	auto global_state = make_uniq<GdalScanGlobalState>(std::move(dataset));
	auto &gstate = *global_state;

	// Open the layer
	OGRLayer *layer = nullptr;
	if (data.sequential_layer_scan) {
		// Get the layer from the dataset by scanning through the layers
		for (int i = 0; i < gstate.dataset->GetLayerCount(); i++) {
			layer = gstate.dataset->GetLayer(i);
			if (i == data.layer_idx) {
				// desired layer found
				break;
			}
			// else scan through and empty the layer
			OGRFeature *feature;
			while ((feature = layer->GetNextFeature()) != nullptr) {
				OGRFeature::DestroyFeature(feature);
			}
		}
	} else {
		// Otherwise get the layer directly
		layer = gstate.dataset->GetLayer(data.layer_idx);
	}

	// Apply spatial filter (if we got one)
	TryApplySpatialFilter(layer, data.spatial_filter.get());
	// TODO: Apply projection pushdown

	// Create arrow stream from layer

	gstate.stream = make_uniq<ArrowArrayStreamWrapper>();

	// set layer options
	if (!layer->GetArrowStream(&gstate.stream->arrow_array_stream, data.layer_creation_options)) {
		throw IOException("Could not get arrow stream");
	}

	gstate.max_threads = GdalTableFunction::MaxThreads(context, input.bind_data.get());

	if (input.CanRemoveFilterColumns()) {
		gstate.projection_ids = input.projection_ids;
		for (const auto &col_idx : input.column_ids) {
			if (col_idx == COLUMN_IDENTIFIER_ROW_ID) {
				gstate.scanned_types.emplace_back(LogicalType::ROW_TYPE);
			} else {
				gstate.scanned_types.push_back(data.all_types[col_idx]);
			}
		}
	}

	return std::move(global_state);
}

//-----------------------------------------------------------------------------
// Init Local
//-----------------------------------------------------------------------------
unique_ptr<LocalTableFunctionState> GdalTableFunction::InitLocal(ExecutionContext &context,
                                                                 TableFunctionInitInput &input,
                                                                 GlobalTableFunctionState *global_state_p) {

	auto &global_state = global_state_p->Cast<ArrowScanGlobalState>();
	auto current_chunk = make_uniq<ArrowArrayWrapper>();
	auto result = make_uniq<GdalScanLocalState>(std::move(current_chunk), context.client);
	result->column_ids = input.column_ids;
	result->filters = input.filters.get();
	if (input.CanRemoveFilterColumns()) {
		result->all_columns.Initialize(context.client, global_state.scanned_types);
	}

	if (!ArrowScanParallelStateNext(context.client, input.bind_data.get(), *result, global_state)) {
		return nullptr;
	}

	return std::move(result);
}

//-----------------------------------------------------------------------------
// Scan
//-----------------------------------------------------------------------------
void GdalTableFunction::Scan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	if (!input.local_state) {
		return;
	}
	auto &data = input.bind_data->Cast<GdalScanFunctionData>();
	auto &state = input.local_state->Cast<GdalScanLocalState>();
	auto &gstate = input.global_state->Cast<GdalScanGlobalState>();

	//! Out of tuples in this chunk
	if (state.chunk_offset >= (idx_t)state.chunk->arrow_array.length) {
		if (!ArrowScanParallelStateNext(context, input.bind_data.get(), state, gstate)) {
			return;
		}
	}
	auto output_size = MinValue<idx_t>(STANDARD_VECTOR_SIZE, state.chunk->arrow_array.length - state.chunk_offset);
	gstate.lines_read += output_size;

	if (gstate.CanRemoveFilterColumns()) {
		state.all_columns.Reset();
		state.all_columns.SetCardinality(output_size);
		ArrowToDuckDB(state, data.arrow_table.GetColumns(), state.all_columns, gstate.lines_read - output_size, false);
		output.ReferenceColumns(state.all_columns, gstate.projection_ids);
	} else {
		output.SetCardinality(output_size);
		ArrowToDuckDB(state, data.arrow_table.GetColumns(), output, gstate.lines_read - output_size, false);
	}

	if (!data.keep_wkb) {
		// Find the geometry columns
		for (idx_t col_idx = 0; col_idx < state.column_ids.size(); col_idx++) {
			auto mapped_idx = state.column_ids[col_idx];
			if (data.geometry_column_ids.find(mapped_idx) != data.geometry_column_ids.end()) {
				// Found a geometry column
				// Convert the WKB columns to a geometry column
				state.arena.Reset();
				auto &wkb_vec = output.data[col_idx];
				Vector geom_vec(core::GeoTypes::GEOMETRY(), output_size);
				UnaryExecutor::ExecuteWithNulls<string_t, core::geometry_t>(
				    wkb_vec, geom_vec, output_size, [&](string_t input, ValidityMask &validity, idx_t out_idx) {
					    if (input.Empty()) {
						    validity.SetInvalid(out_idx);
						    return core::geometry_t {};
					    }
					    auto geom = state.wkb_reader.Deserialize(input);
					    return core::Geometry::Serialize(geom, geom_vec);
				    });
				output.data[col_idx].ReferenceAndSetType(geom_vec);
			}
		}
	}

	output.Verify();
	state.chunk_offset += output.size();
}

unique_ptr<NodeStatistics> GdalTableFunction::Cardinality(ClientContext &context, const FunctionData *data) {
	auto &gdal_data = data->Cast<GdalScanFunctionData>();
	auto result = make_uniq<NodeStatistics>();

	if (gdal_data.has_approximate_feature_count) {
		result->has_estimated_cardinality = true;
		result->estimated_cardinality = gdal_data.approximate_feature_count;
	}
	return result;
}

unique_ptr<TableRef> GdalTableFunction::ReplacementScan(ClientContext &, ReplacementScanInput &input,
                                                        optional_ptr<ReplacementScanData>) {
	auto &table_name = input.table_name;
	auto lower_name = StringUtil::Lower(table_name);
	// Check if the table name ends with some common geospatial file extensions
	if (StringUtil::EndsWith(lower_name, ".gpkg") || StringUtil::EndsWith(lower_name, ".fgb")) {

		auto table_function = make_uniq<TableFunctionRef>();
		vector<unique_ptr<ParsedExpression>> children;
		children.push_back(make_uniq<ConstantExpression>(Value(table_name)));
		table_function->function = make_uniq<FunctionExpression>("ST_Read", std::move(children));
		return std::move(table_function);
	}
	// else not something we can replace
	return nullptr;
}

//------------------------------------------------------------------------------
// Documentation
//------------------------------------------------------------------------------
static constexpr DocTag DOC_TAGS[] = {{"ext", "spatial"}};

static constexpr const char *DOC_DESCRIPTION = R"(
    Read and import a variety of geospatial file formats using the GDAL library.

    The `ST_Read` table function is based on the [GDAL](https://gdal.org/index.html) translator library and enables reading spatial data from a variety of geospatial vector file formats as if they were DuckDB tables.

    > See [ST_Drivers](#st_drivers) for a list of supported file formats and drivers.

    Except for the `path` parameter, all parameters are optional.

    | Parameter | Type | Description |
    | --------- | -----| ----------- |
    | `path` | VARCHAR | The path to the file to read. Mandatory |
    | `sequential_layer_scan` | BOOLEAN | If set to true, the table function will scan through all layers sequentially and return the first layer that matches the given layer name. This is required for some drivers to work properly, e.g., the OSM driver. |
    | `spatial_filter` | WKB_BLOB | If set to a WKB blob, the table function will only return rows that intersect with the given WKB geometry. Some drivers may support efficient spatial filtering natively, in which case it will be pushed down. Otherwise the filtering is done by GDAL which may be much slower. |
    | `open_options` | VARCHAR[] | A list of key-value pairs that are passed to the GDAL driver to control the opening of the file. E.g., the GeoJSON driver supports a FLATTEN_NESTED_ATTRIBUTES=YES option to flatten nested attributes. |
    | `layer` | VARCHAR | The name of the layer to read from the file. If NULL, the first layer is returned. Can also be a layer index (starting at 0). |
    | `allowed_drivers` | VARCHAR[] | A list of GDAL driver names that are allowed to be used to open the file. If empty, all drivers are allowed. |
    | `sibling_files` | VARCHAR[] | A list of sibling files that are required to open the file. E.g., the ESRI Shapefile driver requires a .shx file to be present. Although most of the time these can be discovered automatically. |
    | `spatial_filter_box` | BOX_2D | If set to a BOX_2D, the table function will only return rows that intersect with the given bounding box. Similar to spatial_filter. |
    | `keep_wkb` | BOOLEAN | If set, the table function will return geometries in a wkb_geometry column with the type WKB_BLOB (which can be cast to BLOB) instead of GEOMETRY. This is useful if you want to use DuckDB with more exotic geometry subtypes that DuckDB spatial doesnt support representing in the GEOMETRY type yet. |

    Note that GDAL is single-threaded, so this table function will not be able to make full use of parallelism.

    By using `ST_Read`, the spatial extension also provides “replacement scans” for common geospatial file formats, allowing you to query files of these formats as if they were tables directly.

    ```sql
    SELECT * FROM './path/to/some/shapefile/dataset.shp';
    ```

    In practice this is just syntax-sugar for calling ST_Read, so there is no difference in performance. If you want to pass additional options, you should use the ST_Read table function directly.

    The following formats are currently recognized by their file extension:

    | Format | Extension |
    | ------ | --------- |
    | ESRI ShapeFile | .shp |
    | GeoPackage | .gpkg |
    | FlatGeoBuf | .fgb |
)";

static constexpr const char *DOC_EXAMPLE = R"(
    -- Read a Shapefile
    SELECT * FROM ST_Read('some/file/path/filename.shp');

    -- Read a GeoJSON file
    CREATE TABLE my_geojson_table AS SELECT * FROM ST_Read('some/file/path/filename.json');
)";

//------------------------------------------------------------------------------
// Register
//------------------------------------------------------------------------------

void GdalTableFunction::Register(DatabaseInstance &db) {

	TableFunctionSet set("ST_Read");
	TableFunction scan({LogicalType::VARCHAR}, GdalTableFunction::Scan, GdalTableFunction::Bind,
	                   GdalTableFunction::InitGlobal, GdalTableFunction::InitLocal);

	scan.cardinality = GdalTableFunction::Cardinality;
	scan.get_partition_data = ArrowTableFunction::ArrowGetPartitionData;

	scan.projection_pushdown = true;

	scan.named_parameters["open_options"] = LogicalType::LIST(LogicalType::VARCHAR);
	scan.named_parameters["allowed_drivers"] = LogicalType::LIST(LogicalType::VARCHAR);
	scan.named_parameters["sibling_files"] = LogicalType::LIST(LogicalType::VARCHAR);
	scan.named_parameters["spatial_filter_box"] = core::GeoTypes::BOX_2D();
	scan.named_parameters["spatial_filter"] = core::GeoTypes::WKB_BLOB();
	scan.named_parameters["layer"] = LogicalType::VARCHAR;
	scan.named_parameters["sequential_layer_scan"] = LogicalType::BOOLEAN;
	scan.named_parameters["max_batch_size"] = LogicalType::INTEGER;
	scan.named_parameters["keep_wkb"] = LogicalType::BOOLEAN;
	set.AddFunction(scan);

	ExtensionUtil::RegisterFunction(db, set);
	DocUtil::AddDocumentation(db, "ST_Read", DOC_DESCRIPTION, DOC_EXAMPLE, DOC_TAGS);

	// Replacement scan
	auto &config = DBConfig::GetConfig(db);
	config.replacement_scans.emplace_back(GdalTableFunction::ReplacementScan);
}

} // namespace gdal

} // namespace spatial