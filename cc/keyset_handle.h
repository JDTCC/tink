// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TINK_KEYSET_HANDLE_H_
#define TINK_KEYSET_HANDLE_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "tink/aead.h"
#include "tink/configuration.h"
#include "tink/internal/configuration_impl.h"
#include "tink/internal/key_info.h"
#include "tink/key.h"
#include "tink/key_manager.h"
#include "tink/key_status.h"
#include "tink/keyset_reader.h"
#include "tink/keyset_writer.h"
#include "tink/primitive_set.h"
#include "tink/registry.h"
#include "proto/tink.pb.h"

namespace crypto {
namespace tink {

// KeysetHandle provides abstracted access to Keysets, to limit
// the exposure of actual protocol buffers that hold sensitive
// key material.
class KeysetHandle {
 public:
  // Represents a single entry in a `KeysetHandle`. Some current behavior will
  // be changed in the future.
  class Entry {
   public:
    // May return an internal class in case there is no implementation of the
    // corresponding key class yet.  Returned value only valid for lifetime
    // of entry object.
    const Key& GetKey() const { return *key_; }

    // Status indicates whether or not a key should still be used.
    KeyStatus GetStatus() const { return status_; }

    // ID should be unique (though currently Tink still accepts keysets with
    // repeated IDs).
    int GetId() const { return id_; }

    // Should return true for exactly one entry (though currently Tink still
    // accepts keysets which have no entry marked as primary).
    bool IsPrimary() const { return is_primary_; }

   private:
    friend class KeysetHandle;
    friend class KeysetHandleBuilder;

    Entry(std::unique_ptr<Key> key, KeyStatus status, int id, bool is_primary)
        : key_(std::move(key)),
          status_(status),
          id_(id),
          is_primary_(is_primary) {}

    std::unique_ptr<Key> key_;
    KeyStatus status_;
    int id_;
    bool is_primary_;
  };

  // Returns the number of entries in this keyset.
  int size() const { return keyset_.key_size(); }
  // Validates single `KeysetHandle::Entry` at `index` by making sure that the
  // key entry's type URL is printable and that it has a valid key status.
  crypto::tink::util::Status ValidateAt(int index) const;
  // Validates each individual `KeysetHandle::Entry` in keyset handle by calling
  // `ValidateAt()`.  Also, checks that there is a single enabled primary key.
  crypto::tink::util::Status Validate() const;
  // Returns entry for primary key in this keyset. Crashes if `Validate()`
  // does not return an OK status.  Call `Validate()` prior to calling this
  // method to avoid potentially crashing your program.
  Entry GetPrimary() const;
  // Returns the `KeysetHandle::Entry` at `index`.  Crashes if
  // `ValidateAt(index)` does not return an OK status.  Call `ValidateAt(index)`
  // prior to calling this method to avoid potentially crashing your program.
  Entry operator[](int index) const;

  // Creates a KeysetHandle from an encrypted keyset obtained via `reader`
  // using `master_key_aead` to decrypt the keyset, with monitoring annotations
  // `monitoring_annotations`; by default, `monitoring_annotations` is empty.
  static crypto::tink::util::StatusOr<std::unique_ptr<KeysetHandle>> Read(
      std::unique_ptr<KeysetReader> reader, const Aead& master_key_aead,
      const absl::flat_hash_map<std::string, std::string>&
          monitoring_annotations = {});

  // Creates a KeysetHandle from an encrypted keyset obtained via `reader`
  // using `master_key_aead` to decrypt the keyset, expecting `associated_data`.
  // The keyset is annotated for monitoring with `monitoring_annotations`; by
  // default, `monitoring_annotations` is empty.
  static crypto::tink::util::StatusOr<std::unique_ptr<KeysetHandle>>
  ReadWithAssociatedData(std::unique_ptr<KeysetReader> reader,
                         const Aead& master_key_aead,
                         absl::string_view associated_data,
                         const absl::flat_hash_map<std::string, std::string>&
                             monitoring_annotations = {});

  // Creates a KeysetHandle from a serialized keyset `serialized_keyset` which
  // contains no secret key material, and annotates it with
  // `monitoring_annotations` for monitoring; by default,
  // `monitoring_annotations` is empty. This can be used to load public keysets
  // or envelope encryption keysets.
  static crypto::tink::util::StatusOr<std::unique_ptr<KeysetHandle>>
  ReadNoSecret(const std::string& serialized_keyset,
               const absl::flat_hash_map<std::string, std::string>&
                   monitoring_annotations = {});

  // Returns a KeysetHandle for a new keyset that contains a single fresh key
  // generated according to `key_template`. The keyset is annotated for
  // monitoring with `monitoring_annotations`; by default,
  // `monitoring_annotations` is empty.
  static crypto::tink::util::StatusOr<std::unique_ptr<KeysetHandle>>
  GenerateNew(const google::crypto::tink::KeyTemplate& key_template,
              const absl::flat_hash_map<std::string, std::string>&
                  monitoring_annotations = {});

  // Encrypts the underlying keyset with the provided `master_key_aead`
  // and writes the resulting EncryptedKeyset to the given `writer`,
  // which must be non-null.
  crypto::tink::util::Status Write(KeysetWriter* writer,
                                   const Aead& master_key_aead) const;

  // Encrypts the underlying keyset with the provided `master_key_aead`, using
  // `associated_data`. and writes the resulting EncryptedKeyset to the given
  // `writer`, which must be non-null.
  crypto::tink::util::Status WriteWithAssociatedData(
      KeysetWriter* writer, const Aead& master_key_aead,
      absl::string_view associated_data) const;

  // Returns KeysetInfo, a "safe" Keyset that doesn't contain any actual
  // key material, thus can be used for logging or monitoring.
  google::crypto::tink::KeysetInfo GetKeysetInfo() const;

  // Writes the underlying keyset to `writer` only if the keyset does not
  // contain any secret key material.
  // This can be used to persist public keysets or envelope encryption keysets.
  // Users that need to persist cleartext keysets can use
  // `CleartextKeysetHandle`.
  crypto::tink::util::Status WriteNoSecret(KeysetWriter* writer) const;

  // Returns a new KeysetHandle that contains public keys corresponding
  // to the private keys from this handle.
  // Returns an error if this handle contains keys that are not private keys.
  crypto::tink::util::StatusOr<std::unique_ptr<KeysetHandle>>
  GetPublicKeysetHandle() const;

  // Creates a wrapped primitive using this keyset handle and config, which
  // stores necessary primitive wrappers and key type managers.
  template <class P>
  crypto::tink::util::StatusOr<std::unique_ptr<P>> GetPrimitive(
      const Configuration& config) const;

  // Creates a wrapped primitive using this keyset handle and the global
  // registry, which stores necessary primitive wrappers and key type managers.
  template <class P>
  crypto::tink::util::StatusOr<std::unique_ptr<P>> GetPrimitive() const;

  // Creates a wrapped primitive corresponding to this keyset. Uses the given
  // KeyManager, as well as the KeyManager and PrimitiveWrapper objects in the
  // global registry to create the primitive. The given KeyManager is used for
  // keys supported by it. For those, the registry is ignored.
  template <class P>
  ABSL_DEPRECATED("Register the keymanager and use GetPrimitive")
  crypto::tink::util::StatusOr<std::unique_ptr<P>> GetPrimitive(
      const KeyManager<P>* custom_manager) const;

 private:
  // The classes below need access to get_keyset();
  friend class CleartextKeysetHandle;
  friend class KeysetManager;

  // TestKeysetHandle::GetKeyset() provides access to get_keyset().
  friend class TestKeysetHandle;

  // KeysetHandleBuilder::Build() needs access to KeysetHandle(Keyset).
  friend class KeysetHandleBuilder;

  // Creates a handle that contains the given keyset.
  explicit KeysetHandle(google::crypto::tink::Keyset keyset);
  // Creates a handle that contains the given keyset.
  explicit KeysetHandle(std::unique_ptr<google::crypto::tink::Keyset> keyset);
  // Creates a handle that contains the given `keyset` and
  // `monitoring_annotations`.
  KeysetHandle(google::crypto::tink::Keyset keyset,
               const absl::flat_hash_map<std::string, std::string>&
                   monitoring_annotations)
      : keyset_(std::move(keyset)),
        monitoring_annotations_(monitoring_annotations) {}
  KeysetHandle(std::unique_ptr<google::crypto::tink::Keyset> keyset,
               const absl::flat_hash_map<std::string, std::string>&
                   monitoring_annotations)
      : keyset_(std::move(*keyset)),
        monitoring_annotations_(monitoring_annotations) {}

  // Generates a key from `key_template` and adds it `keyset`.
  static crypto::tink::util::StatusOr<uint32_t> AddToKeyset(
      const google::crypto::tink::KeyTemplate& key_template, bool as_primary,
      google::crypto::tink::Keyset* keyset);

  // Generates a key from `key_template` and adds it to the keyset handle.
  crypto::tink::util::StatusOr<uint32_t> AddKey(
      const google::crypto::tink::KeyTemplate& key_template, bool as_primary);

  // Returns keyset held by this handle.
  const google::crypto::tink::Keyset& get_keyset() const;

  // Creates a set of primitives corresponding to the keys with
  // (status == ENABLED) in the keyset given in 'keyset_handle',
  // assuming all the corresponding key managers are present (keys
  // with (status != ENABLED) are skipped).
  //
  // The returned set is usually later "wrapped" into a class that
  // implements the corresponding Primitive-interface.
  template <class P>
  crypto::tink::util::StatusOr<std::unique_ptr<PrimitiveSet<P>>> GetPrimitives(
      const KeyManager<P>* custom_manager) const;

  google::crypto::tink::Keyset keyset_;
  absl::flat_hash_map<std::string, std::string> monitoring_annotations_;
};

///////////////////////////////////////////////////////////////////////////////
// Implementation details of templated methods.

template <class P>
crypto::tink::util::StatusOr<std::unique_ptr<PrimitiveSet<P>>>
KeysetHandle::GetPrimitives(const KeyManager<P>* custom_manager) const {
  crypto::tink::util::Status status = ValidateKeyset(get_keyset());
  if (!status.ok()) return status;
  typename PrimitiveSet<P>::Builder primitives_builder;
  primitives_builder.AddAnnotations(monitoring_annotations_);
  for (const google::crypto::tink::Keyset::Key& key : get_keyset().key()) {
    if (key.status() == google::crypto::tink::KeyStatusType::ENABLED) {
      std::unique_ptr<P> primitive;
      if (custom_manager != nullptr &&
          custom_manager->DoesSupport(key.key_data().type_url())) {
        auto primitive_result = custom_manager->GetPrimitive(key.key_data());
        if (!primitive_result.ok()) return primitive_result.status();
        primitive = std::move(primitive_result.value());
      } else {
        auto primitive_result = Registry::GetPrimitive<P>(key.key_data());
        if (!primitive_result.ok()) return primitive_result.status();
        primitive = std::move(primitive_result.value());
      }
      if (key.key_id() == get_keyset().primary_key_id()) {
        primitives_builder.AddPrimaryPrimitive(std::move(primitive),
                                               KeyInfoFromKey(key));
      } else {
        primitives_builder.AddPrimitive(std::move(primitive),
                                        KeyInfoFromKey(key));
      }
    }
  }
  auto primitives = std::move(primitives_builder).Build();
  if (!primitives.ok()) return primitives.status();
  return absl::make_unique<PrimitiveSet<P>>(*std::move(primitives));
}

template <class P>
crypto::tink::util::StatusOr<std::unique_ptr<P>> KeysetHandle::GetPrimitive(
    const Configuration& config) const {
  return internal::ConfigurationImpl::get_registry(config).WrapKeyset<P>(
      keyset_, monitoring_annotations_);
}

template <class P>
crypto::tink::util::StatusOr<std::unique_ptr<P>> KeysetHandle::GetPrimitive()
    const {
  return internal::RegistryImpl::GlobalInstance().WrapKeyset<P>(
      keyset_, monitoring_annotations_);
}

template <class P>
crypto::tink::util::StatusOr<std::unique_ptr<P>> KeysetHandle::GetPrimitive(
    const KeyManager<P>* custom_manager) const {
  if (custom_manager == nullptr) {
    return crypto::tink::util::Status(absl::StatusCode::kInvalidArgument,
                                      "custom_manager must not be null");
  }
  auto primitives_result = this->GetPrimitives<P>(custom_manager);
  if (!primitives_result.ok()) {
    return primitives_result.status();
  }
  return Registry::Wrap<P>(std::move(primitives_result.value()));
}

}  // namespace tink
}  // namespace crypto

#endif  // TINK_KEYSET_HANDLE_H_
