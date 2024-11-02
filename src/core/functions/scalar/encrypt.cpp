#define DUCKDB_EXTENSION_MAIN

#define TEST_KEY "0123456789112345"
#define MAX_BUFFER_SIZE 1024

#include "simple_encryption_extension.hpp"
#include "simple_encryption_state.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/encryption_state.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "mbedtls_wrapper.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/types/blob.hpp"
#include "duckdb/main/connection_manager.hpp"
#include "simple_encryption/core/functions/scalar/encrypt.hpp"
#include "simple_encryption/core/functions/scalar.hpp"

namespace simple_encryption {

namespace core {

shared_ptr<EncryptionState> InitializeCryptoState() {
  // for now just harcode MBEDTLS here
  shared_ptr<EncryptionState> encryption_state =
      duckdb_mbedtls::MbedTlsWrapper::AESGCMStateMBEDTLSFactory()
          .CreateEncryptionState();
  return encryption_state;
}

shared_ptr<EncryptionState> InitializeEncryption() {

  // For now, hardcode everything
  const string key = TEST_KEY;
  unsigned char tag[16];
  unsigned char iv[16];
  memcpy((void *)iv, "12345678901", 12);

  // TODO; construct nonce based on immutable ROW_ID + hash(col_name)
  iv[12] = 0x00;
  iv[13] = 0x00;
  iv[14] = 0x00;
  iv[15] = 0x00;

  auto encryption_state = InitializeCryptoState();
  encryption_state->InitializeEncryption(iv, 16, &key);

  return encryption_state;
}

shared_ptr<EncryptionState> InitializeDecryption() {

  // For now, hardcode everything
  const string key = TEST_KEY;
  unsigned char tag[16];
  unsigned char iv[16];
  memcpy((void *)iv, "12345678901", 12);
  //
  //  // TODO; construct nonce based on immutable ROW_ID + hash(col_name)
  iv[12] = 0x00;
  iv[13] = 0x00;
  iv[14] = 0x00;
  iv[15] = 0x00;

  auto decryption_state = InitializeCryptoState();
  decryption_state->InitializeDecryption(iv, 16, &key);

  return decryption_state;
}

inline const uint8_t *DecryptValue(uint8_t *buffer, size_t size) {

  // Initialize Encryption
  auto encryption_state = InitializeDecryption();
  uint8_t decryption_buffer[MAX_BUFFER_SIZE];
  uint8_t *temp_buf = decryption_buffer;

  encryption_state->Process(buffer, size, temp_buf, size);

  return temp_buf;
}

static void EncryptData(DataChunk &args, ExpressionState &state,
                        Vector &result) {

  auto encryption_state = InitializeEncryption();

  uint8_t encryption_buffer[MAX_BUFFER_SIZE];
  uint8_t *buffer = encryption_buffer;

  auto &name_vector = args.data[0];

  UnaryExecutor::Execute<string_t, string_t>(
      name_vector, result, args.size(), [&](string_t name) {
        auto size = name.GetSize();
        auto value = reinterpret_cast<const uint8_t *>(name.GetData());

        encryption_state->Process(value, size, buffer, size);

        D_ASSERT(MAX_BUFFER_SIZE ==
                 sizeof(encryption_buffer) / sizeof(encryption_buffer[0]));

        string_t encrypted_data = reinterpret_cast<const char *>(buffer);
        auto printable_encrypted_data = Blob::ToString(encrypted_data);

#ifdef DEBUG
        // cast encrypted data to blob back and forth to check whether data will be lost with casting
        auto unblobbed_data = Blob::ToBlob(printable_encrypted_data);
        auto encrypted_unblobbed_data =
            reinterpret_cast<const uint8_t *>(unblobbed_data.data());

        if (memcmp(encrypted_unblobbed_data, buffer, size) != 0) {
          throw InvalidInputException(
              "Original Encrypted Data differs from Unblobbed Encrypted Data");
        }

        auto decrypted_data = DecryptValue(buffer, size);
        if (memcmp(decrypted_data, value, size) != 0) {
          throw InvalidInputException(
              "Original Data differs from Decrypted Data");
        }
#endif

        return StringVector::AddString(
            result,
            name.GetString() + " is encrypted as: " + printable_encrypted_data);
      });
}

ScalarFunctionSet GetEncryptionFunction() {

  ScalarFunctionSet set("encrypt");
  // support all available types for encryption
  for (auto &type : LogicalType::AllTypes()) {
    set.AddFunction(ScalarFunction({type}, LogicalType::BLOB, EncryptData));
  }

  return set;
}

//------------------------------------------------------------------------------
// Register functions
//------------------------------------------------------------------------------

void CoreScalarFunctions::RegisterEncryptDataScalarFunction(
    DatabaseInstance &db) {
  ExtensionUtil::RegisterFunction(
      db,
      ScalarFunction("encrypt",
                     {LogicalType::VARCHAR},
                     LogicalType::VARCHAR, EncryptData));
}

//------------------------------------------------------------------------------
// Register functions
//------------------------------------------------------------------------------
// set function
//void CoreScalarFunctions::RegisterEncryptDataScalarFunction(DatabaseInstance &db) {
////  auto encrypt_value = ScalarFunction("encrypt", {LogicalType::VARCHAR}, LogicalType::VARCHAR, EncryptValue);
////  ExtensionUtil::RegisterFunction(instance, encrypt_value);
//  ExtensionUtil::RegisterFunction(db, GetEncryptionFunction());
////  ExtensionUtil::RegisterFunction(db, EncryptData());
//}


// TODO: see if it works like this
// And different types

}
}