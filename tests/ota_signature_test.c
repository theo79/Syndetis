// Actual production signature verifier, linked against pinned Mbed TLS on host.
#include "ota_signature.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static size_t read_file(const char *name,unsigned char *p,size_t cap)
{
 FILE *f=fopen(name,"rb");CHECK(f);size_t n=fread(p,1,cap,f);CHECK(!ferror(f)&&feof(f));fclose(f);return n;
}
int main(int argc,char **argv)
{
 CHECK(argc==5);CHECK(psa_crypto_init()==PSA_SUCCESS);
 unsigned char image[4096],pem[2048],wrong[2048],private[2048],hash[32];
 size_t n=read_file(argv[1],image,sizeof(image)),pn=read_file(argv[2],pem,sizeof(pem)),
  wn=read_file(argv[3],wrong,sizeof(wrong)),sn=read_file(argv[4],private,sizeof(private)),used=0;
 CHECK(n>=416);pem[pn++]=0;wrong[wn++]=0;private[sn++]=0;
 mbedtls_pk_context key,other,secret;mbedtls_pk_init(&key);mbedtls_pk_init(&other);mbedtls_pk_init(&secret);
 CHECK(ota_signature_load(&key,pem,pn));CHECK(ota_signature_load(&other,wrong,wn));
 CHECK(!ota_signature_load(&secret,private,sn));
 CHECK(psa_hash_compute(PSA_ALG_SHA_256,image,n-128,hash,sizeof(hash),&used)==PSA_SUCCESS&&used==32);
 unsigned char *trailer=image+n-128;CHECK(ota_signature_verify(&key,hash,trailer));
 CHECK(!ota_signature_verify(&other,hash,trailer));
 for(unsigned i=0;i<48;++i){trailer[i]^=1;CHECK(!ota_signature_verify(&key,hash,trailer));trailer[i]^=1;}
 for(unsigned i=48;i<128;++i){trailer[i]^=0xff;CHECK(!ota_signature_verify(&key,hash,trailer));trailer[i]^=0xff;}
 hash[0]^=1;CHECK(!ota_signature_verify(&key,hash,trailer));
 mbedtls_pk_free(&key);mbedtls_pk_free(&other);mbedtls_pk_free(&secret);
 puts("PASS: production P-256 verifier against pinned Mbed TLS/PSA: Python-signed image, wrong key/private key, mutated signed metadata/signature and image digest");
}
