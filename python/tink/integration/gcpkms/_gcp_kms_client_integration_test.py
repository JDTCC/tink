# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Tests for tink.python.tink.integration.gcp_kms_aead."""

import os

from absl.testing import absltest

import tink
from tink import aead
from tink.integration import gcpkms
from tink.testing import helper

CREDENTIAL_PATH = os.path.join(helper.tink_py_testdata_path(),
                               'gcp/credential.json')
KEY_URI = 'gcp-kms://projects/tink-test-infrastructure/locations/global/keyRings/unit-and-integration-testing/cryptoKeys/aead-key'
LOCAL_KEY_URI = 'gcp-kms://projects/tink-test-infrastructure/locations/europe-west1/keyRings/unit-and-integration-test/cryptoKeys/aead-key'
BAD_KEY_URI = 'aws-kms://arn:aws:kms:us-east-2:235739564943:key/3ee50705-5a82-4f5b-9753-05c4f473922f'

if 'TEST_SRCDIR' in os.environ:
  # Set root certificates for gRPC in Bazel Test which are needed on MacOS
  os.environ['GRPC_DEFAULT_SSL_ROOTS_FILE_PATH'] = os.path.join(
      os.environ['TEST_SRCDIR'], 'google_root_pem/file/downloaded')


def setUpModule():
  aead.register()


class GcpKmsAeadTest(absltest.TestCase):

  def test_encrypt_decrypt(self):
    gcp_client = gcpkms.GcpKmsClient(KEY_URI, CREDENTIAL_PATH)
    gcp_aead = gcp_client.get_aead(KEY_URI)

    plaintext = b'helloworld'
    ciphertext = gcp_aead.encrypt(plaintext, b'')
    self.assertEqual(plaintext, gcp_aead.decrypt(ciphertext, b''))

    plaintext = b'hello'
    associated_data = b'world'
    ciphertext = gcp_aead.encrypt(plaintext, associated_data)
    self.assertEqual(plaintext, gcp_aead.decrypt(ciphertext, associated_data))

  def test_encrypt_decrypt_localized_uri(self):
    gcp_client = gcpkms.GcpKmsClient(LOCAL_KEY_URI, CREDENTIAL_PATH)
    gcp_aead = gcp_client.get_aead(LOCAL_KEY_URI)

    plaintext = b'helloworld'
    ciphertext = gcp_aead.encrypt(plaintext, b'')
    self.assertEqual(plaintext, gcp_aead.decrypt(ciphertext, b''))

    plaintext = b'hello'
    associated_data = b'world'
    ciphertext = gcp_aead.encrypt(plaintext, associated_data)
    self.assertEqual(plaintext, gcp_aead.decrypt(ciphertext, associated_data))

  def test_encrypt_with_bad_uri(self):
    with self.assertRaises(tink.TinkError):
      gcp_client = gcpkms.GcpKmsClient(KEY_URI, CREDENTIAL_PATH)
      gcp_client.get_aead(BAD_KEY_URI)

  def test_corrupted_ciphertext(self):
    gcp_client = gcpkms.GcpKmsClient(KEY_URI, CREDENTIAL_PATH)
    gcp_aead = gcp_client.get_aead(KEY_URI)

    plaintext = b'helloworld'
    ciphertext = gcp_aead.encrypt(plaintext, b'')
    self.assertEqual(plaintext, gcp_aead.decrypt(ciphertext, b''))

    # Corrupt each byte once and check that decryption fails
    # NOTE: Only starting at 4th byte here, as the 3rd byte is malleable
    #      (see b/146633745).
    for byte_idx in range(3, len(ciphertext)):
      tmp_ciphertext = list(ciphertext)
      tmp_ciphertext[byte_idx] ^= 1
      corrupted_ciphertext = bytes(tmp_ciphertext)
      with self.assertRaises(tink.TinkError):
        gcp_aead.decrypt(corrupted_ciphertext, b'')

  def test_registration_client_bound_to_uri_works(self):
    # Register GCP KMS Client bound to KEY_URI.
    gcpkms.GcpKmsClient.register_client(KEY_URI, CREDENTIAL_PATH)

    # Create a keyset handle for KEY_URI and use it. This works.
    handle = tink.new_keyset_handle(
        aead.aead_key_templates.create_kms_aead_key_template(KEY_URI)
    )
    gcp_aead = handle.primitive(aead.Aead)
    ciphertext = gcp_aead.encrypt(b'plaintext', b'associated_data')
    self.assertEqual(
        b'plaintext', gcp_aead.decrypt(ciphertext, b'associated_data')
    )

    # But it fails for LOCAL_KEY_URI, since the URI is different.
    with self.assertRaises(tink.TinkError):
      handle2 = tink.new_keyset_handle(
          aead.aead_key_templates.create_kms_aead_key_template(LOCAL_KEY_URI)
      )
      gcp_aead = handle2.primitive(aead.Aead)
      gcp_aead.encrypt(b'plaintext', b'associated_data')


if __name__ == '__main__':
  absltest.main()
