#ifndef _SHIM_LINUX_CRYPTO_HASH_H
#define _SHIM_LINUX_CRYPTO_HASH_H

#include <shim/linux/types.h>

struct crypto_shash;
struct shash_desc;

struct crypto_shash *crypto_alloc_shash(const char *alg_name, u32 type, u32 mask);
void crypto_free_shash(struct crypto_shash *tfm);
int crypto_shash_digest(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *out);

#endif /* _SHIM_LINUX_CRYPTO_HASH_H */
