#pragma once
#include "discovery_logic.h"
#define DISCOVERY_NAME_CONCURRENCY 2
#define DISCOVERY_NAME_BUDGET_MS 2500
#define DISCOVERY_NAME_TIMEOUT_MS 700
#define DISCOVERY_NAME_MAX_ATTEMPTS 16
#define DISCOVERY_NAME_PACE_MS 50
#ifdef __cplusplus
#define NAME_INLINE static inline constexpr
#else
#define NAME_INLINE static inline
#endif
/* Preserve case/labels; strip one root dot. Reject controls, invalid labels and
 * overlong names rather than manufacture a truncated DNS identity. */
NAME_INLINE bool discovery_name_valid(const char *in, char out[64])
{
    if (!out) return false;
    out[0]=0;
    if (!in) return false;
    size_t n=0;
    while (n<65 && in[n]) ++n;
    if (!n || n==65) return false;
    if (in[n-1]=='.') --n;
    if (!n || n>63) return false;
    unsigned label=0;
    for (size_t i=0;i<n;++i) {
        char c=in[i];
        if (c=='.') {
            if (!label || in[i-1]=='-') return false;
            label=0;
        } else {
            bool allowed=(c>='a' && c<='z') || (c>='A' && c<='Z') ||
                         (c>='0' && c<='9') || c=='-' || c=='_';
            if (!allowed || (!label && c=='-') || ++label>63) return false;
        }
    }
    if (!label || in[n-1]=='-') return false;
    for (size_t i=0;i<n;++i) out[i]=in[i];
    out[n]=0;
    return true;
}
/* One result updates only the original IP in the current result set. Duplicate
 * completions are harmless. Transport has no late callbacks or retained pointers. */
NAME_INLINE bool discovery_name_apply(discovery_device_t *table, uint32_t count,
    uint32_t target, const char *name, uint32_t session, uint32_t current_session,
    uint32_t revision, uint32_t current_revision, bool link, bool active)
{
    if (!table || count>DISCOVERY_MAX_HOSTS || !session || session!=current_session ||
        revision!=current_revision || !link || !active) return false;
    char clean[64]={0};
    if (!discovery_name_valid(name,clean)) return false;
    for (uint32_t i=0;i<count;++i) if (table[i].valid && table[i].ip==target) {
        if (table[i].hostname_present) return false;
        for (unsigned j=0;j<64;++j) table[i].hostname[j]=clean[j];
        table[i].hostname_present=true;
        return true;
    }
    return false;
}
#undef NAME_INLINE
