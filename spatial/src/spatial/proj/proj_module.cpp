#include "spatial/common.hpp"

#include "spatial/proj/module.hpp"
#include "spatial/proj/functions.hpp"

#include "proj.h"
#include "sqlite3.h"

namespace spatial {

namespace proj {

// We embed the whole proj.db in the proj_db.c file, which we then link into the extension binary
// We can then use the sqlite3 "memvfs" (which we also statically link to) to point to the proj.db database in memory
// To genereate the proj_db.c file, we use the following command:
// `xxd -i proj.db > proj_db.c`
// Then rename the array to proj_db and the length to proj_db_len if necessary
// We link these from the proj_db.c file externally instead of #include:ing so our IDE doesnt go haywire
extern "C" unsigned char proj_db[];
extern "C" unsigned int proj_db_len;
extern "C" int sqlite3_memvfs_init(sqlite3 *, char **, const sqlite3_api_routines *);

PJ_CONTEXT *ProjModule::GetThreadProjContext() {

	auto ctx = proj_context_create();

	// We set the default context proj.db path to the one in the binary here
	// Otherwise GDAL will try to load the proj.db from the system
	// Any PJ_CONTEXT we create after this will inherit these settings
	auto path = StringUtil::Format("file:/proj.db?immutable=1&ptr=%llu&sz=%lu&max=%lu", (void *)proj_db, proj_db_len, proj_db_len);

	proj_context_set_sqlite3_vfs_name(ctx, "memvfs");
	auto ok = proj_context_set_database_path(ctx, path.c_str(), nullptr, nullptr);
	if (!ok) {
		throw InternalException("Could not set proj.db path");
	}

	// Dont log errors to stderr
	proj_log_level(ctx, PJ_LOG_NONE);

	// Dont allow network
	proj_context_set_enable_network(ctx, false);

	return ctx;
}

// TODO: ignore memvfs, load into :memory: at runtime instead...?

// IMPORTANT: Make sure this module is loaded before any other modules that use proj (like GDAL)
void ProjModule::Register(DatabaseInstance &db) {
	// we use the sqlite "memvfs" to store the proj.db database in the extension binary itself
	// this way we don't have to worry about the user having the proj.db database installed
	// on their system. We therefore have to tell proj to use memvfs as the sqlite3 vfs and
	// point it to the segment of the binary that contains the proj.db database

	sqlite3_initialize();
	sqlite3_memvfs_init(nullptr, nullptr, nullptr);
	auto vfs = sqlite3_vfs_find("memvfs");
	if (!vfs) {
		throw InternalException("Could not find sqlite memvfs extension");
	}
	sqlite3_vfs_register(vfs, 0);

	// We set the default context proj.db path to the one in the binary here
	// Otherwise GDAL will try to load the proj.db from the system
	// Any PJ_CONTEXT we create after this will inherit these settings (on this thread?)
	auto path = StringUtil::Format("file:/proj.db?immutable=1&ptr=%llu&sz=%lu&max=%lu", (void *)proj_db, proj_db_len, proj_db_len);

	proj_context_set_sqlite3_vfs_name(nullptr, "memvfs");

	auto ok = proj_context_set_database_path(nullptr, path.c_str(), nullptr, nullptr);
	if (!ok) {
		throw InternalException("Could not set proj.db path");
	}

	// Register functions
	ProjFunctions::Register(db);
}

} // namespace proj

} // namespace spatial
