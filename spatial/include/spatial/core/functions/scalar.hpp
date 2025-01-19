#pragma once
#include "spatial/common.hpp"

namespace spatial {

namespace core {

struct CoreScalarFunctions {
public:
	static void Register(DatabaseInstance &db) {
		RegisterStArea(db);
		RegisterStAsGeoJSON(db);
		RegisterStAsText(db);
		RegisterStAsWKB(db);
		RegisterStAsHEXWKB(db);
		RegisterStAsSVG(db);
		RegisterStCentroid(db);
		RegisterStCollect(db);
		RegisterStCollectionExtract(db);
		RegisterStContains(db);
		RegisterStDimension(db);
		RegisterStDistance(db);
		RegisterStDump(db);
		RegisterStEndPoint(db);
		RegisterStExtent(db);
		RegisterStExteriorRing(db);
		RegisterStFlipCoordinates(db);
		RegisterStForce(db);
		RegisterStGeometryType(db);
		RegisterStGeomFromHEXWKB(db);
		RegisterStGeomFromText(db);
		RegisterStGeomFromWKB(db);
		RegisterStHas(db);
		RegisterStHaversine(db);
		RegisterStHilbert(db);
		RegisterStIntersects(db);
		RegisterStIntersectsExtent(db);
		RegisterStIsClosed(db);
		RegisterStIsEmpty(db);
		RegisterStLength(db);
		RegisterStMakeEnvelope(db);
		RegisterStMakeLine(db);
		RegisterStMakePolygon(db);
		RegisterStMulti(db);
		RegisterStNGeometries(db);
		RegisterStNInteriorRings(db);
		RegisterStNPoints(db);
		RegisterStPerimeter(db);
		RegisterStPoint(db);
		RegisterStPointN(db);
		RegisterStPoints(db);
		RegisterStQuadKey(db);
		RegisterStRemoveRepeatedPoints(db);
		RegisterStStartPoint(db);
		RegisterStX(db);
		RegisterStXMax(db);
		RegisterStXMin(db);
		RegisterStY(db);
		RegisterStYMax(db);
		RegisterStYMin(db);
		RegisterStZ(db);
		RegisterStZMax(db);
		RegisterStZMin(db);
		RegisterStM(db);
		RegisterStMMax(db);
		RegisterStMMin(db);
	}

private:
	// ST_Area
	static void RegisterStArea(DatabaseInstance &db);

	// ST_AsGeoJSON
	static void RegisterStAsGeoJSON(DatabaseInstance &db);

	// ST_AsText
	static void RegisterStAsText(DatabaseInstance &db);

	// ST_AsHextWKB
	static void RegisterStAsHEXWKB(DatabaseInstance &db);

	// ST_AsSVG
	static void RegisterStAsSVG(DatabaseInstance &db);

	// ST_AsWKB
	static void RegisterStAsWKB(DatabaseInstance &db);

	// ST_Centroid
	static void RegisterStCentroid(DatabaseInstance &db);

	// ST_Collect
	static void RegisterStCollect(DatabaseInstance &db);

	// ST_CollectionExtract
	static void RegisterStCollectionExtract(DatabaseInstance &db);

	// ST_Contains
	static void RegisterStContains(DatabaseInstance &db);

	// ST_Dimension
	static void RegisterStDimension(DatabaseInstance &db);

	// ST_Distance
	static void RegisterStDistance(DatabaseInstance &db);

	// ST_Dump
	static void RegisterStDump(DatabaseInstance &db);

	// ST_EndPoint
	static void RegisterStEndPoint(DatabaseInstance &db);

	// ST_Extent
	static void RegisterStExtent(DatabaseInstance &db);

	// ST_ExteriorRing
	static void RegisterStExteriorRing(DatabaseInstance &db);

	// ST_FlipCoordinates
	static void RegisterStFlipCoordinates(DatabaseInstance &db);

	// ST_Force(2D/3D)
	static void RegisterStForce(DatabaseInstance &db);

	// ST_GeometryType
	static void RegisterStGeometryType(DatabaseInstance &db);

	// ST_GeomFromHEXWKB
	static void RegisterStGeomFromHEXWKB(DatabaseInstance &db);

	// ST_GeomFromText
	static void RegisterStGeomFromText(DatabaseInstance &db);

	// ST_GeomFromWKB
	static void RegisterStGeomFromWKB(DatabaseInstance &db);

	// ST_Has(M/Z) + ST_ZMFlag
	static void RegisterStHas(DatabaseInstance &db);

	// ST_Haversine
	static void RegisterStHaversine(DatabaseInstance &db);

	// ST_Hilbert
	static void RegisterStHilbert(DatabaseInstance &db);

	// ST_Intersects
	static void RegisterStIntersects(DatabaseInstance &db);

	// ST_IntersectsExtent (&&)
	static void RegisterStIntersectsExtent(DatabaseInstance &db);

	// ST_IsClosed
	static void RegisterStIsClosed(DatabaseInstance &db);

	// ST_IsEmpty
	static void RegisterStIsEmpty(DatabaseInstance &db);

	// ST_Length
	static void RegisterStLength(DatabaseInstance &db);

	// ST_MakeEnvelope
	static void RegisterStMakeEnvelope(DatabaseInstance &db);

	// ST_MakeLine
	static void RegisterStMakeLine(DatabaseInstance &db);

	// ST_MakePolygon
	static void RegisterStMakePolygon(DatabaseInstance &db);

	// ST_Multi
	static void RegisterStMulti(DatabaseInstance &db);

	// ST_NGeometries
	static void RegisterStNGeometries(DatabaseInstance &db);

	// ST_NInteriorRings
	static void RegisterStNInteriorRings(DatabaseInstance &db);

	// ST_NPoints
	static void RegisterStNPoints(DatabaseInstance &db);

	// ST_Perimeter
	static void RegisterStPerimeter(DatabaseInstance &db);

	// ST_Point
	static void RegisterStPoint(DatabaseInstance &db);

	// ST_PointN
	static void RegisterStPointN(DatabaseInstance &db);

	// ST_Points
	static void RegisterStPoints(DatabaseInstance &db);

	// ST_RemoveRepeatedPoints
	static void RegisterStRemoveRepeatedPoints(DatabaseInstance &db);

	// ST_QuadKey
	static void RegisterStQuadKey(DatabaseInstance &db);

	// ST_StartPoint
	static void RegisterStStartPoint(DatabaseInstance &db);

	// ST_X
	static void RegisterStX(DatabaseInstance &db);

	// ST_XMax
	static void RegisterStXMax(DatabaseInstance &db);

	// ST_XMin
	static void RegisterStXMin(DatabaseInstance &db);

	// ST_Y
	static void RegisterStY(DatabaseInstance &db);

	// ST_YMax
	static void RegisterStYMax(DatabaseInstance &db);

	// ST_YMin
	static void RegisterStYMin(DatabaseInstance &db);

	// ST_Z
	static void RegisterStZ(DatabaseInstance &db);

	// ST_ZMax
	static void RegisterStZMax(DatabaseInstance &db);

	// ST_ZMin
	static void RegisterStZMin(DatabaseInstance &db);

	// ST_M
	static void RegisterStM(DatabaseInstance &db);

	// ST_MMax
	static void RegisterStMMax(DatabaseInstance &db);

	// ST_MMin
	static void RegisterStMMin(DatabaseInstance &db);
};

bool WKBToGeometryCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

bool GeometryToWKBCast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

} // namespace core

} // namespace spatial