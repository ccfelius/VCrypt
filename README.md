# VCrypt

This repository is based on https://github.com/duckdb/extension-template, check it out if you want to build and ship your own DuckDB extension.

---

VCrypt, short for _Vectorized Cryptography_, allows to efficiently encrypt and decrypt values within DuckDB. It is leveraging DuckDB compression methods to compress away metadata such as nonces, which are used to randomize the encryption. Because of its design, VCrypt often uses _vectorized processing_ to encrypt and decrypt values in batch.

NB: this extension is highly experimental and might not work correctly yet.
For more information about the internals, you can read the corresponding [paper](https://openproceedings.org/2025/conf/edbt/paper-347.pdf). 

## Usage

### Key management

Create a DuckDB secret;

```
CREATE SECRET key_name (
    TYPE VCRYPT,
    TOKEN 'secret_key'
    LENGTH 16);
```

Supported key lenghts are 16, 24 and 32 bytes. In future versions, we are aiming for compatibility with a Key Management System. In addition, we are working on single- and double key-wrapping and rotating for better security.
To generate an encryption key, you can also use the `generate_key()` scalar function. Key lengths of 16, 24 and 32 bytes are supported. For example

```
SELECT generate_key(16) as key;
```

will produce a 16-byte base64 encoded key.

### Encrypted Types
In VCrypt, an encrypted value is represented as a `STRUCT` type internally. Because this is somewhat complex to define, we implemented `ENCRYPTED` types.
Each encrypted type is prefixed by `E_`; for example, an encrypted `INTEGER` is represented as `E_INTEGER`.

All non-nested existing types in DuckDB are currently supported by VCrypt.

### Encrypting and Decrypting

For Vectorized Encryption (batch encryption/decryption), Encrypt or Decrypt with:

```
encrypt(value, 'key_name')
decrypt(value, 'key_name')
```

For _per-value_ encryption, or to encrypt values that are most certainly _not_ accessed together, we recommend to use

```
encrypt_naive(value, 'key_name')
decrypt_naive(value, 'key_name')
```

Note that this approach will be significantly slower if multiple values are being [en/de]crypted, and the storage overhead increases due to a seperate number only used once (nonce) generated for every value (which will be resolved in future versions).

### Notes

We are now only supporting MBEDTLS/OPENSSL `AES-CTR`, but are working on supporting multiple ciphers. We aim to support at least:

- `AES-GCM` (authenticated, randomized)
- `AES-OCB` (authenticated, randomized)
- `AES-ECB`
- `AES-CBC`
- `AES-CFB`
- `AES-OFB`

## Building
### Managing dependencies
DuckDB extensions uses VCPKG for dependency management. Enabling VCPKG is very simple: follow the [installation instructions](https://vcpkg.io/en/getting-started) or just run the following:
```shell
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_TOOLCHAIN_PATH=`pwd`/vcpkg/scripts/buildsystems/vcpkg.cmake
```
Note: VCPKG is only required for extensions that want to rely on it for dependency management. If you want to develop an extension without dependencies, or want to do your own dependency management, just skip this step. Note that the example extension uses VCPKG to build with a dependency for instructive purposes, so when skipping this step the build may not work without removing the dependency.

### Build steps
Now to build the extension, run:
```sh
make
```
The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/vcrypt/vcrypt.duckdb_extension
```
- `duckdb` is the binary for the duckdb shell with the extension code automatically loaded.
- `unittest` is the test runner of duckdb. Again, the extension is already linked into the binary.
- `vcrypt.duckdb_extension` is the loadable binary as it would be distributed.

## Running the extension
To run the extension code, simply start the shell with `./build/release/duckdb`.

Now we can use the features from the extension directly in DuckDB. The template contains a single scalar function `vcrypt()` that takes a string arguments and returns a string:
```
D select vcrypt('Jane') as result;
┌───────────────┐
│    result     │
│    varchar    │
├───────────────┤
│ Simple_encryption Jane 🐥 │
└───────────────┘
```

## Running the tests
Different tests can be created for DuckDB extensions. The primary way of testing DuckDB extensions should be the SQL tests in `./test/sql`. These SQL tests can be run using:
```sh
make test
```

### Installing the deployed binaries
To install your extension binaries from S3, you will need to do two things. Firstly, DuckDB should be launched with the
`allow_unsigned_extensions` option set to true. How to set this will depend on the client you're using. Some examples:

CLI:
```shell
duckdb -unsigned
```

Python:
```python
con = duckdb.connect(':memory:', config={'allow_unsigned_extensions' : 'true'})
```

NodeJS:
```js
db = new duckdb.Database(':memory:', {"allow_unsigned_extensions": "true"});
```

Secondly, you will need to set the repository endpoint in DuckDB to the HTTP url of your bucket + version of the extension
you want to install. To do this run the following SQL query in DuckDB:
```sql
SET custom_extension_repository='bucket.s3.eu-west-1.amazonaws.com/<your_extension_name>/latest';
```
Note that the `/latest` path will allow you to install the latest extension version available for your current version of
DuckDB. To specify a specific version, you can pass the version instead.

After running these steps, you can install and load your extension using the regular INSTALL/LOAD commands in DuckDB:
```sql
INSTALL vcrypt
LOAD vcrypt
```
