#ifndef ATOM_COMMON_ASAR_ASAR_CRYPTO_H
#define ATOM_COMMON_ASAR_ASAR_CRYPTO_H

#include "atom/common/node_includes.h"

// Copy the implementation of CipherBase from node_crypto.h/.cc(referred as node implemetatioin) because node 
// implementation is tightly bind with js invocation. And it is not easy to invoke js codes from c++ because
// this class is invoked when initializing environment.
//
// If we can easily construct v8::Local<v8::Object>, then we can use node implementation. But i met crash issue
// when creating v8::Local<v8::Object> like this in DecryptHeader() in archive.cc:
//
//      v8::Local<v8::ObjectTemplate> dummy = v8::ObjectTemplate::New();
//      v8::Local<v8::Value> dummy_value = dummy->NewInstance();
//
// It will crash in `v8::ObjectTemplate::New()`. 

namespace asar {

class CipherBase{
 public:

  static bool DecryptData(char **indata, int inlen);

  ~CipherBase() {
    if (!initialised_)
      return;
    delete[] auth_tag_;
    EVP_CIPHER_CTX_cleanup(&ctx_);
  }

 private:
  enum CipherKind {
    kCipher,
    kDecipher
  };

  void Init(const char* cipher_type, const char* key_buf, int key_buf_len);
  void InitIv(const char* cipher_type,
              const char* key,
              int key_len,
              const char* iv,
              int iv_len);
  bool Update(const char* data, int len, unsigned char** out, int* out_len);
  bool Final(unsigned char** out, int *out_len);
  bool SetAutoPadding(bool auto_padding);

  bool IsAuthenticatedMode() const;
  bool GetAuthTag(char** out, unsigned int* out_len) const;
  bool SetAuthTag(const char* data, unsigned int len);
  bool SetAAD(const char* data, unsigned int len);

  CipherBase(CipherKind kind)
      : cipher_(nullptr),
        initialised_(false),
        kind_(kind),
        auth_tag_(nullptr),
        auth_tag_len_(0) {
  }

 private:
  EVP_CIPHER_CTX ctx_; /* coverity[member_decl] */
  const EVP_CIPHER* cipher_; /* coverity[member_decl] */
  bool initialised_;
  CipherKind kind_;
  char* auth_tag_;
  unsigned int auth_tag_len_;
};

} // end of namespace asar

#endif // ATOM_COMMON_ASAR_ASAR_CRYPTO_H
