#define DUCKDB_EXTENSION_MAIN

// what is the maximum size of biggest type in duckdb
#define MAX_BUFFER_SIZE 1024

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
#include "simple_encryption_state.hpp"
#include "duckdb/main/client_context.hpp"
#include "simple_encryption/core/functions/function_data/encrypt_function_data.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "simple_encryption/core/types.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"

// temporary

namespace simple_encryption {

namespace core {


template <typename T>
typename std::enable_if<std::is_integral<T>::value || std::is_floating_point<T>::value, T>::type
ProcessAndCastEncrypt(shared_ptr<EncryptionState> encryption_state, Vector &result, T plaintext_data, uint8_t *buffer_p) {
  // actually, you can just for process already give the pointer to the result, thus skip buffer
  T encrypted_data;
  encryption_state->Process(reinterpret_cast<unsigned char*>(&plaintext_data), sizeof(int32_t), reinterpret_cast<unsigned char*>(&encrypted_data), sizeof(int32_t));
  return encrypted_data;
}

template <typename T>
typename std::enable_if<std::is_same<T, string_t>::value, T>::type
ProcessAndCastEncrypt(shared_ptr<EncryptionState> encryption_state, Vector &result, T plaintext_data, uint8_t *buffer_p) {

  auto &children = StructVector::GetEntries(result);
  // take the third vector of the struct
  auto &result_vector = children[2];

  // first encrypt the bytes of the string into a temp buffer_p
  auto input_data = data_ptr_t(plaintext_data.GetData());
  auto value_size = plaintext_data.GetSize();
  encryption_state->Process(input_data, value_size, buffer_p, value_size);

  // Convert the encrypted data to Base64
  auto encrypted_data = string_t(reinterpret_cast<const char*>(buffer_p), value_size);
  size_t base64_size = Blob::ToBase64Size(encrypted_data);

  // convert to Base64 into a newly allocated string in the result vector
  T base64_data = StringVector::EmptyString(*result_vector, base64_size);
  Blob::ToBase64(encrypted_data, base64_data.GetDataWriteable());

  return base64_data;
}

template <typename T>
typename std::enable_if<std::is_same<T, string_t>::value, T>::type
ProcessAndCastDecrypt(shared_ptr<EncryptionState> encryption_state, Vector &result, T base64_data, uint8_t *buffer_p) {

  auto &children = StructVector::GetEntries(result);
  auto &result_vector = children[2];

  // first encrypt the bytes of the string into a temp buffer_p
  size_t encrypted_size = Blob::FromBase64Size(base64_data);
  size_t decrypted_size = encrypted_size;
  Blob::FromBase64(base64_data, reinterpret_cast<data_ptr_t>(buffer_p), encrypted_size);
  D_ASSERT(encrypted_size <= base64_data.GetSize());

  string_t decrypted_data = StringVector::EmptyString(*result_vector, decrypted_size);
  encryption_state->Process(buffer_p, encrypted_size, reinterpret_cast<unsigned char*>(decrypted_data.GetDataWriteable()), decrypted_size);

  return decrypted_data;
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value || std::is_floating_point<T>::value, T>::type
ProcessAndCastDecrypt(shared_ptr<EncryptionState> encryption_state, Vector &result, T encrypted_data, uint8_t *buffer_p) {
  // actually, you can just for process already give the pointer to the result, thus skip buffer
  T decrypted_data;
  encryption_state->Process(reinterpret_cast<unsigned char*>(&encrypted_data), sizeof(T), reinterpret_cast<unsigned char*>(&decrypted_data), sizeof(T));
  return decrypted_data;
}

shared_ptr<EncryptionState> GetEncryptionState(ExpressionState &state){

  auto &func_expr = (BoundFunctionExpression &)state.expr;
  auto &info = (EncryptFunctionData &)*func_expr.bind_info;

  // refactor this into GetSimpleEncryptionState(info.context);
  auto simple_encryption_state =
      info.context.registered_state->Get<SimpleEncryptionState>(
          "simple_encryption");

  return simple_encryption_state->encryption_state;

}

// todo; template
LogicalType CreateEINTtypeStruct() {
  return LogicalType::STRUCT({{"nonce_hi", LogicalType::UBIGINT},
                              {"nonce_lo", LogicalType::UBIGINT},
                              {"value", LogicalType::INTEGER}});
}


LogicalType CreateEVARtypeStruct() {
  return LogicalType::STRUCT({{"nonce_hi", LogicalType::UBIGINT},
                              {"nonce_lo", LogicalType::UBIGINT},
                              {"value", LogicalType::VARCHAR}});
}

template <typename T>
void EncryptToEtype(LogicalType result_struct, Vector &input_vector, const string key_t, uint64_t size, ExpressionState &state, Vector &result){

  // Reset the reference of the result vector
  Vector struct_vector(result_struct, size);
  result.ReferenceAndSetType(struct_vector);

  // Set vector types
  auto &children = StructVector::GetEntries(result);
  auto &nonce_hi = children[0];
  auto &nonce_lo = children[1];
  nonce_hi->SetVectorType(VectorType::CONSTANT_VECTOR);
  nonce_lo->SetVectorType(VectorType::FLAT_VECTOR);

  // For every bulk insert we generate a new initialization vector
  uint64_t iv[2];
  auto encryption_state = GetEncryptionState(state);
  iv[0] = iv[1] = 0;
  encryption_state->GenerateRandomData(reinterpret_cast<unsigned char*>(iv), 12);

  // Initialize encryption state
  encryption_state->InitializeEncryption(reinterpret_cast<unsigned char*>(iv), 16, reinterpret_cast<const string *>(&key_t));

  using ENCRYPTED_TYPE = StructTypeTernary<uint64_t, uint64_t, T>;
  using PLAINTEXT_TYPE = PrimitiveType<T>;

  // TODO: put this in the state of the extension
  uint8_t encryption_buffer[MAX_BUFFER_SIZE];
  uint8_t *buffer_p = encryption_buffer;

  GenericExecutor::ExecuteUnary<PLAINTEXT_TYPE, ENCRYPTED_TYPE>(input_vector, result, size, [&](PLAINTEXT_TYPE input) {

    // increment the low part of the nonce
    iv[1]++;
    T encrypted_data = ProcessAndCastEncrypt(encryption_state, result, input.val, buffer_p);

    return ENCRYPTED_TYPE {iv[0], iv[1], encrypted_data};
  });
}

template <typename T>
void DecryptFromEtype(Vector &input_vector, const string key_t, uint64_t size, ExpressionState &state, Vector &result){

  // TODO: put this in the state of the extension
  // Create encryption buffer
  uint8_t encryption_buffer[MAX_BUFFER_SIZE];
  uint8_t *buffer_p = encryption_buffer;

 uint64_t iv[2];
 iv[0] = iv[1] = 0;
 uint64_t nonce_hi = 0;

  // check if nonce is repeated for the data chunk
  // get children of the struct to calculate nonce
  // auto &children = StructVector::GetEntries(input_vector);
  // auto &nonce_hi_vector = children[0];
  // if (nonce_hi_vector->GetVectorType() == VectorType::CONSTANT_VECTOR) {
  //   nonce_hi = ConstantVector::GetData<uint64_t>(*nonce_hi_vector)[0];
  //   memcpy(iv, &nonce_hi, 8);
  // }

 auto encryption_state = GetEncryptionState(state);
 encryption_state->InitializeDecryption(reinterpret_cast<const_data_ptr_t>(iv), 16, &key_t);

 using ENCRYPTED_TYPE = StructTypeTernary<uint64_t, uint64_t, T>;
 using PLAINTEXT_TYPE = PrimitiveType<T>;

 GenericExecutor::ExecuteUnary<ENCRYPTED_TYPE, PLAINTEXT_TYPE>(input_vector, result, size, [&](ENCRYPTED_TYPE input) {

       memcpy(iv, &input.a_val, 8);
       memcpy(iv + 1, &input.b_val, 8);

       T decrypted_data = ProcessAndCastDecrypt(encryption_state, result, input.c_val, buffer_p);
       return decrypted_data;
     });
}

static void EncryptDataToEtype(DataChunk &args, ExpressionState &state, Vector &result) {

  auto &input_vector = args.data[0];
  auto vector_type = input_vector.GetType();
  auto size = args.size();

  // Get the encryption key from client input
  auto &key_vector = args.data[1];
  D_ASSERT(key_vector.GetVectorType() == VectorType::CONSTANT_VECTOR);
  const string key_t =
      ConstantVector::GetData<string_t>(key_vector)[0].GetString();

  if (vector_type.IsNumeric()) {
    switch (vector_type.id()){
      case LogicalTypeId::TINYINT:
      case LogicalTypeId::UTINYINT:
        return EncryptToEtype<int8_t>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      case LogicalTypeId::SMALLINT:
      case LogicalTypeId::USMALLINT:
        return EncryptToEtype<int16_t>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      case LogicalTypeId::INTEGER:
        return EncryptToEtype<int32_t>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      case LogicalTypeId::UINTEGER:
        return EncryptToEtype<uint32_t>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      case LogicalTypeId::BIGINT:
        return EncryptToEtype<int64_t>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      case LogicalTypeId::UBIGINT:
        return EncryptToEtype<uint64_t>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      case LogicalTypeId::FLOAT:
        return EncryptToEtype<float>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      case LogicalTypeId::DOUBLE:
        return EncryptToEtype<double>(CreateEINTtypeStruct(), input_vector, key_t, size, state, result);
      default:
        throw NotImplementedException("Unsupported numeric type for encryption");
      }
  } else if (vector_type.id() == LogicalTypeId::VARCHAR) {
    return EncryptToEtype<string_t>(CreateEVARtypeStruct(), input_vector, key_t, size, state, result);
  } else if (vector_type.IsNested()) {
    throw NotImplementedException(
        "Nested types are not supported for encryption");
  } else if (vector_type.IsTemporal()) {
    throw NotImplementedException(
        "Temporal types are not supported for encryption");
  }
}

static void DecryptDataFromEtype(DataChunk &args, ExpressionState &state, Vector &result) {

  auto &func_expr = (BoundFunctionExpression &)state.expr;
  auto &info = (EncryptFunctionData &)*func_expr.bind_info;

  // refactor this into GetSimpleEncryptionState(info.context);
  auto simple_encryption_state =
      info.context.registered_state->Get<SimpleEncryptionState>(
          "simple_encryption");

  auto size = args.size();
  auto &input_vector = args.data[0];
  auto &key_vector = args.data[1];
  D_ASSERT(key_vector.GetVectorType() == VectorType::CONSTANT_VECTOR);

  // Fetch the encryption key as a constant string
  const string key_t =
      ConstantVector::GetData<string_t>(key_vector)[0].GetString();

  auto &children = StructVector::GetEntries(input_vector);
  // get type of vector containing encrypted values
  auto vector_type = children[2]->GetType();

  if (vector_type.IsNumeric()) {
    switch (vector_type.id()) {
    case LogicalTypeId::TINYINT:
    case LogicalTypeId::UTINYINT:
      return DecryptFromEtype<int8_t>(input_vector, key_t, size, state, result);
    case LogicalTypeId::SMALLINT:
    case LogicalTypeId::USMALLINT:
      return DecryptFromEtype<int16_t>(input_vector, key_t, size, state, result);
    case LogicalTypeId::INTEGER:
      return DecryptFromEtype<int32_t>(input_vector, key_t, size, state, result);
    case LogicalTypeId::UINTEGER:
      return DecryptFromEtype<uint32_t>(input_vector, key_t, size, state, result);
    case LogicalTypeId::BIGINT:
      return DecryptFromEtype<int64_t>(input_vector, key_t, size, state, result);
    case LogicalTypeId::UBIGINT:
      return DecryptFromEtype<uint64_t>(input_vector, key_t, size, state, result);
    case LogicalTypeId::FLOAT:
      return DecryptFromEtype<float>(input_vector, key_t, size, state, result);
    case LogicalTypeId::DOUBLE:
      return DecryptFromEtype<double>(input_vector, key_t, size, state, result);
    default:
      throw NotImplementedException("Unsupported numeric type for decryption");
    }
  } else if (vector_type.id() == LogicalTypeId::VARCHAR) {
    return EncryptToEtype<string_t>(CreateEVARtypeStruct(), input_vector, key_t,
                                    size, state, result);
  } else if (vector_type.IsNested()) {
    throw NotImplementedException(
        "Nested types are not supported for decryption");
  } else if (vector_type.IsTemporal()) {
    throw NotImplementedException(
        "Temporal types are not supported for decryption");
  }
}

ScalarFunctionSet GetEncryptionStructFunction() {
  ScalarFunctionSet set("encrypt_etypes");

  for (auto &type: LogicalType::AllTypes()) {
    set.AddFunction(
        ScalarFunction({type, LogicalType::VARCHAR},
                       LogicalType::STRUCT({{"nonce_hi", LogicalType::UBIGINT},
                                            {"nonce_lo", LogicalType::UBIGINT},
                                            {"value", type}}),
                       EncryptDataToEtype, EncryptFunctionData::EncryptBind));
  }

  return set;
}

ScalarFunctionSet GetDecryptionStructFunction() {
  ScalarFunctionSet set("decrypt_etypes");

  for (auto &type: LogicalType::AllTypes()) {
    for (auto &nonce_type_a : LogicalType::Numeric()) {
      for (auto &nonce_type_b : LogicalType::Numeric()) {
        set.AddFunction(ScalarFunction(
            {LogicalType::STRUCT({{"nonce_hi", nonce_type_a},
                                  {"nonce_lo", nonce_type_b},
                                  {"value", type}}),
             LogicalType::VARCHAR},
            type, DecryptDataFromEtype, EncryptFunctionData::EncryptBind));
      }
    }
  }

  // TODO: Fix EINT encryption
//  set.AddFunction(ScalarFunction({EncryptionTypes::E_INT(), LogicalType::VARCHAR}, LogicalTypeId::INTEGER, DecryptDataChunkStruct,
//                                 EncryptFunctionData::EncryptBind));

  return set;
}



//------------------------------------------------------------------------------
// Register functions
//------------------------------------------------------------------------------

void CoreScalarFunctions::RegisterEncryptDataStructScalarFunction(
    DatabaseInstance &db) {
  ExtensionUtil::RegisterFunction(db, GetEncryptionStructFunction());
  ExtensionUtil::RegisterFunction(db, GetDecryptionStructFunction());
}
}
}
