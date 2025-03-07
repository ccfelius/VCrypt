#define DUCKDB_EXTENSION_MAIN
#define MAX_BUFFER_SIZE 1024

#include "simple_encryption_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/types/blob.hpp"
#include "simple_encryption_state.hpp"
#include "duckdb/main/connection_manager.hpp"
#include <simple_encryption_extension_callback.hpp>
#include "simple_encryption/core/module.hpp"
#include "simple_encryption/core/crypto/crypto_primitives.hpp"
#include "etype/encrypted_type.hpp"

namespace duckdb {

static void LoadInternal(DatabaseInstance &instance) {
  // register functions in the core module
  simple_encryption::core::CoreModule::Register(instance);
  simple_encryption::core::CoreModule::RegisterType(instance);

  // Register the SimpleEncryptionState for all connections
  auto &config = DBConfig::GetConfig(instance);

  // set pointer to OpenSSL encryption state
  config.encryption_util = make_shared_ptr<AESStateSSLFactory>();

  // Add extension callback
  config.extension_callbacks.push_back(
      make_uniq<SimpleEncryptionExtensionCallback>());

  // Register the SimpleEncryptionState for all connections
  for (auto &connection :
       ConnectionManager::Get(instance).GetConnectionList()) {
    connection->registered_state->Insert(
        "simple_encryption",
        make_shared_ptr<VCryptState>(connection));
  }
}

void SimpleEncryptionExtension::Load(DuckDB &db) {
  LoadInternal(*db.instance); }
std::string SimpleEncryptionExtension::Name() { return "simple_encryption"; }

std::string SimpleEncryptionExtension::Version() const {
#ifdef EXT_VERSION_SIMPLE_ENCRYPTION
  return EXT_VERSION_SIMPLE_ENCRYPTION;
#else
  return "V0.0.1";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void simple_encryption_init(duckdb::DatabaseInstance &db) {
  duckdb::DuckDB db_wrapper(db);
  db_wrapper.LoadExtension<duckdb::SimpleEncryptionExtension>();
}

DUCKDB_EXTENSION_API const char *simple_encryption_version() {
  return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
