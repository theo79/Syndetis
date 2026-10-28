#include "ota_signature.h"
#include <string.h>
#include "psa/crypto.h"
static uint32_t read32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
bool ota_signature_load(mbedtls_pk_context *key,const uint8_t *pem,size_t n)
{
    return n>1 && pem[n-1]==0 && mbedtls_pk_parse_public_key(key,pem,n)==0 &&
        mbedtls_pk_get_key_type(key)==PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1) &&
        mbedtls_pk_get_bitlen(key)==256 &&
        mbedtls_pk_can_do_psa(key,PSA_ALG_ECDSA(PSA_ALG_SHA_256),PSA_KEY_USAGE_VERIFY_HASH);
}
bool ota_signature_verify(mbedtls_pk_context *key,const uint8_t digest[32],const uint8_t t[128])
{
    static const uint8_t magic[8]={'S','Y','N','D','O','6',0,1};
    uint8_t message[44],signed_hash[32];size_t used=0;
    uint32_t length=read32(t+12);
    if(memcmp(t,magic,8) || length<8 || length>80 || memcmp(digest,t+16,32))return false;
    for(uint32_t i=48+length;i<128;++i)if(t[i]!=0)return false;
    memcpy(message,t,12);memcpy(message+12,t+16,32);
    if(psa_hash_compute(PSA_ALG_SHA_256,message,sizeof(message),signed_hash,sizeof(signed_hash),&used)!=PSA_SUCCESS || used!=32)return false;
    return mbedtls_pk_verify(key,MBEDTLS_MD_SHA256,signed_hash,32,t+48,length)==0;
}
