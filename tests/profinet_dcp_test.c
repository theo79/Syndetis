#include "profinet_dcp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t local[] = {2, 0, 0, 0, 0, 1};
static const uint32_t xid = 0x10203040;
static const uint8_t response[] = {
    2,0,0,0,0,1, 2,0,0,0,0,2, 0x88,0x92,
    0xfe,0xff,5,1, 0x10,0x20,0x30,0x40, 0,0, 0,0x3e,
    2,2,0,7, 0,0,'c','e','l','l','1',0,
    2,1,0,6, 0,0,'D','e','m','o',
    1,2,0,14, 0,0x82, 192,0,2,2, 255,255,255,0, 192,0,2,1,
    2,3,0,6, 0,0,0x12,0x34,0x56,0x78,
    2,4,0,4, 0,0,3,0,
    0x80,1,0,0
};

static void dump(const uint8_t *frame, size_t length)
{
    for (size_t i = 0; i < length; ++i) printf("%02x", frame[i]);
    putchar('\n');
}

int main(int argc, char **argv)
{
    static const uint8_t expected[] = {
        1,0x0e,0xcf,0,0,0, 2,0,0,0,0,1, 0x88,0x92,0xfe,0xfe,
        5,0,0x10,0x20,0x30,0x40,0,0x80,0,4,0xff,0xff,0,0
    };
    uint8_t request[DCP_REQUEST_SIZE];
    dcp_identify_all(request, local, xid);
    assert(!memcmp(request, expected, sizeof(expected)));
    for (size_t i = sizeof(expected); i < sizeof(request); ++i) assert(request[i] == 0);
    if (argc == 2 && !strcmp(argv[1], "--wire")) {
        dump(request, sizeof(request));
        dump(response, sizeof(response));
        return 0;
    }
    dcp_device_t d;
    assert(dcp_parse(response, sizeof(response), local, xid, &d) == DCP_PARSED);
    assert(d.mac[5] == 2 && !strcmp(d.name, "cell1") && !strcmp(d.vendor, "Demo"));
    assert(d.fields == (DCP_NAME | DCP_VENDOR | DCP_IP | DCP_ID | DCP_ROLE));
    assert(d.ip[3] == 2 && d.mask[2] == 255 && d.gateway[3] == 1 && d.ip_info == 0x82);
    assert(d.vendor_id == 0x1234 && d.device_id == 0x5678 && d.role == 3);
    assert(dcp_parse(response, sizeof(response), local, xid + 1, &d) == DCP_IGNORE);
    for (size_t n = 0; n < sizeof(response); ++n)
        assert(dcp_parse(response, n, local, xid, &d) == (n < 22 ? DCP_IGNORE : DCP_MALFORMED));
    uint8_t edit[sizeof(response)];
    const unsigned ignore_offsets[] = {0, 6, 12, 15, 16, 17};
    for (size_t i = 0; i < sizeof(ignore_offsets) / sizeof(*ignore_offsets); ++i) {
        memcpy(edit, response, sizeof(edit));
        edit[ignore_offsets[i]] ^= 1;
        assert(dcp_parse(edit, sizeof(edit), local, xid, &d) == DCP_IGNORE);
    }
    memcpy(edit, response, sizeof(edit));
    edit[29] = 250; // Block length exceeds DCPDataLength.
    assert(dcp_parse(edit, sizeof(edit), local, xid, &d) == DCP_MALFORMED);
    edit[29] = 7;
    edit[25] = 11; // Odd station-name block needs one alignment byte.
    assert(dcp_parse(edit, sizeof(edit), local, xid, &d) == DCP_MALFORMED);
    edit[25] = 12;
    edit[32] = 10;
    assert(dcp_parse(edit, sizeof(edit), local, xid, &d) == DCP_PARSED);
    assert(d.fields == DCP_NAME && d.text_limited && d.name[0] == '?');
    edit[25] = 0;
    assert(dcp_parse(edit, sizeof(edit), local, xid, &d) == DCP_PARSED && d.fields == 0);

    uint8_t tagged[sizeof(response) + 8];
    const uint8_t tags[] = {0x88,0xa8,0,2,0x81,0,0,7};
    memcpy(tagged, response, 12);
    memcpy(tagged + 12, tags + 4, 4);
    memcpy(tagged + 16, response + 12, sizeof(response) - 12);
    assert(dcp_parse(tagged, sizeof(tagged) - 4, local, xid, &d) == DCP_PARSED);
    memcpy(tagged + 12, tags, 8);
    memcpy(tagged + 20, response + 12, sizeof(response) - 12);
    assert(dcp_parse(tagged, sizeof(tagged), local, xid, &d) == DCP_PARSED);

    dcp_inventory_t inventory = {0};
    dcp_begin(&inventory, xid, 100);
    dcp_summary_t *s = &inventory.summary;
    dcp_observe(&inventory, &d);
    dcp_observe(&inventory, &d);
    assert(s->count == 1 && !s->duplicate_name && !s->duplicate_ip && s->reported_conflict);
    ++d.mac[5];
    dcp_observe(&inventory, &d);
    assert(s->count == 2 && s->duplicate_name && s->duplicate_ip);
    assert(inventory.devices[0].duplicate_name && inventory.devices[1].duplicate_ip);
    d.name[0] = 0;
    memset(d.ip, 0, sizeof(d.ip));
    d.ip_info = 0;
    dcp_observe(&inventory, &d);
    assert(!s->duplicate_name && !s->duplicate_ip && !inventory.devices[0].duplicate_name);
    for (unsigned i = 2; i <= DCP_CAPACITY; ++i) {
        ++d.mac[5];
        dcp_observe(&inventory, &d);
    }
    assert(s->count == DCP_CAPACITY && s->overflow == 1 && !s->duplicate_name && !s->duplicate_ip);
    dcp_finish(s, DCP_COMPLETE, 5100);
    assert(s->valid && dcp_partial(s));
    dcp_link_down(s, 5200);
    assert(!s->valid && s->state == DCP_COMPLETE);
    dcp_begin(&inventory, xid + 1, 9000);
    assert(s->valid && s->count == 0 && !s->overflow && dcp_active(s->state));
    dcp_finish(s, DCP_CANCELLED, 9001);
    assert(dcp_partial(s) && !dcp_active(s->state));
    puts("PASS: PROFINET Identify wire format, bounded parser, inventory and conflict tracking");
}
