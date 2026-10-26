// Actual portable firmware parser/table/button logic, compiled with a host C compiler.
// All frames are synthetic. This does not test the Ethernet MAC, RF or hardware GPIOs.
#include "neighbors.h"
#include "button_logic.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint8_t packet[1600];
static size_t length, header;
static neighbor_t record;
static neighbor_table_t table;

static void byte(unsigned v) { CHECK(length < sizeof(packet)); packet[length++] = v; }
static void word(unsigned v) { byte(v >> 8); byte(v); }
static void bytes(const void *p, size_t n) { CHECK(length + n <= sizeof(packet)); memcpy(packet + length, p, n); length += n; }
static void ethernet(bool cdp, bool tagged)
{
    const uint8_t lldp_mac[6] = {1,0x80,0xc2,0,0,0x0e};
    const uint8_t cdp_mac[6] = {1,0,12,204,204,204};
    const uint8_t source[6] = {2,0,0,0,0,1};
    length = 0; memset(packet, 0, sizeof(packet));
    bytes(cdp ? cdp_mac : lldp_mac, 6); bytes(source, 6);
    if (tagged) { word(0x8100); word(123); }
    word(cdp ? 0 : 0x88cc); header = length;
}
static void lldp_tlv(unsigned type, const void *p, size_t n) { word((type << 9) | n); bytes(p, n); }
static void lldp_base(unsigned ttl, bool tagged)
{
    ethernet(false, tagged);
    const uint8_t chassis[] = {4,2,0,0,0,0,1}, port[] = {5,'G','i','1','/','0','/','1'};
    uint8_t life[] = {ttl >> 8, ttl};
    lldp_tlv(1, chassis, sizeof(chassis)); lldp_tlv(2, port, sizeof(port)); lldp_tlv(3, life, 2);
}
static void lldp_optional(void)
{
    const uint8_t mgmt[] = {5,1,192,0,2,9,2,0,0,0,7,0};
    const uint8_t pvid[] = {0,0x80,0xc2,1,0,100};
    lldp_tlv(5, "switch-01", 9); lldp_tlv(4, "Office uplink", 13);
    lldp_tlv(8, mgmt, sizeof(mgmt)); lldp_tlv(127, pvid, sizeof(pvid));
}
static void lldp_end(void) { word(0); }
static void cdp_tlv(unsigned type, const void *p, size_t n) { word(type); word(n + 4); bytes(p, n); }
static void cdp_finish(void)
{
    size_t payload = length - header;
    packet[header - 2] = payload >> 8; packet[header - 1] = payload;
    // Compute independently as a folded one's-complement sum of network words.
    size_t start = header + 8;
    packet[start + 2] = packet[start + 3] = 0;
    unsigned sum = 0;
    for (size_t i = start; i < length; i += 2) {
        unsigned value;
        if (i + 1 < length) value = 256 * packet[i] + packet[i + 1];
        else value = packet[i] < 128 ? packet[i] : 0xff00 + packet[i] - 1;
        sum += value;
        sum = sum % 65536 + sum / 65536;
    }
    unsigned check = 65535 - (sum % 65536 + sum / 65536);
    packet[start + 2] = check >> 8; packet[start + 3] = check;
}
static void cdp_base(unsigned ttl, bool optional, bool tagged)
{
    ethernet(true, tagged);
    const uint8_t snap[] = {0xaa,0xaa,3,0,0,12,0x20,0};
    bytes(snap, 8); byte(2); byte(ttl); word(0);
    cdp_tlv(1, "switch-cdp", 10); cdp_tlv(3, "Gig1/0/7", 8);
    if (optional) {
        const uint8_t address[] = {0,0,0,1,1,1,0xcc,0,4,192,0,2,10};
        const uint8_t management[] = {0,0,0,1,1,1,0xcc,0,4,192,0,2,20};
        const uint8_t vlan[] = {0,200};
        cdp_tlv(2, address, sizeof(address)); cdp_tlv(0x16, management, sizeof(management));
        cdp_tlv(10, vlan, 2);
    }
    cdp_finish();
}
static parse_result_t parse(void) { return neighbor_parse(packet, length, &record); }
static void valid(void) { CHECK(parse() == PARSE_VALID); }

static void parser_tests(void)
{
    lldp_base(120, false); lldp_optional(); lldp_end(); valid();
    CHECK(record.protocol == PROTOCOL_LLDP && record.ttl == 120);
    CHECK(!strcmp(record.name, "switch-01") && !strcmp(record.port, "Gi1/0/1"));
    CHECK(!strcmp(record.chassis, "02:00:00:00:00:01") && !strcmp(record.description, "Office uplink"));
    CHECK(!strcmp(record.management, "192.0.2.9") && record.management_kind == 1 && record.vlan == 100);
    CHECK(record.fields == (FIELD_NAME|FIELD_PORT|FIELD_CHASSIS|FIELD_DESCRIPTION|FIELD_MANAGEMENT|FIELD_VLAN));
    size_t full = length;
    for (size_t n = 0; n < full; ++n) CHECK(neighbor_parse(packet, n, &record) != PARSE_VALID);
    lldp_base(20, true); lldp_end(); valid();
    CHECK(record.fields == (FIELD_CHASSIS|FIELD_PORT));
    lldp_base(20, false); lldp_tlv(5, "", 0); lldp_end(); valid();
    CHECK((record.fields & FIELD_NAME) && record.name[0] == 0); // Empty differs from absent.
    lldp_base(20, false);
    const uint8_t ipv6[] = {17,2,0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1,2,0,0,0,1,0};
    lldp_tlv(8, ipv6, sizeof(ipv6)); lldp_end(); valid();
    CHECK(!strcmp(record.management, "2001:db8:0:0:0:0:0:1"));
    lldp_base(20, false); lldp_tlv(5, "first", 5); lldp_tlv(5, "second", 6); lldp_end();
    CHECK(parse() == PARSE_INVALID);
    lldp_base(20, false); lldp_optional(); lldp_end(); packet[length - 4] = 0xff;
    CHECK(parse() != PARSE_VALID);
    lldp_base(20, false); word((5 << 9) | 511); byte('a'); CHECK(parse() == PARSE_INVALID);
    lldp_base(20, false); const uint8_t bad_mgmt[] = {5,1,1,2,3,4,2,0,0,0,1,2};
    lldp_tlv(8, bad_mgmt, sizeof(bad_mgmt)); lldp_end(); CHECK(parse() == PARSE_INVALID);
    lldp_base(20, true); lldp_end(); packet[16]=0x81; packet[17]=0; CHECK(parse() == PARSE_LIMIT);
    packet[12]=0x88; packet[13]=0xa8; CHECK(parse() == PARSE_LIMIT);
    lldp_base(20, false); lldp_end(); CHECK(neighbor_parse(packet, 1519, &record) == PARSE_LIMIT);

    cdp_base(180, true, false); valid();
    CHECK(record.protocol == PROTOCOL_CDP && record.ttl == 180 && record.vlan == 200);
    CHECK(!strcmp(record.name, "switch-cdp") && !strcmp(record.port, "Gig1/0/7"));
    CHECK(!strcmp(record.management, "192.0.2.20") && record.management_kind == 1);
    CHECK(!(record.fields & FIELD_CHASSIS) && !(record.fields & FIELD_DESCRIPTION));
    full = length;
    for (size_t n = 0; n < full; ++n) CHECK(neighbor_parse(packet, n, &record) != PARSE_VALID);
    packet[length - 1] ^= 1; CHECK(parse() == PARSE_INVALID); // Checksum corruption.
    cdp_base(180, false, true); valid(); CHECK(!(record.fields & FIELD_MANAGEMENT));
    packet[header + 5] = 0; CHECK(parse() == PARSE_INVALID); // Wrong SNAP OUI.
    cdp_base(180, false, false);
    const uint8_t fallback[] = {0,0,0,1,1,1,0xcc,0,4,192,0,2,8};
    cdp_tlv(2, fallback, sizeof(fallback)); cdp_finish(); valid();
    CHECK(record.management_kind == 2 && !strcmp(record.management,"192.0.2.8"));
    cdp_base(180, false, false);
    const uint8_t cdp_ipv6[] = {0,0,0,1,2,8,0xaa,0xaa,3,0,0,0,0x86,0xdd,0,16,
                              0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,9};
    cdp_tlv(0x16, cdp_ipv6, sizeof(cdp_ipv6)); cdp_finish(); valid();
    CHECK(record.management_kind == 1 && !strcmp(record.management,"2001:db8:0:0:0:0:0:9"));
    cdp_base(180, false, false); packet[header + 12 + 3] = 3; cdp_finish(); CHECK(parse() == PARSE_INVALID);
    cdp_base(180, false, false); packet[header - 2] = 0x05; packet[header - 1] = 0xdc; CHECK(parse() == PARSE_INVALID);
    cdp_base(180, false, false);
    const uint8_t huge_count[] = {255,255,255,255}; cdp_tlv(2, huge_count, 4); cdp_finish(); CHECK(parse() == PARSE_INVALID);
    // Known hand-calculated checksum vectors: even, odd low byte and Cisco odd high byte.
    const uint8_t vectors[3][15] = {
        {2,60,0x82,0x4d,0,1,0,5,'s',0,3,0,5,'p'},
        {2,60,0x80,0xf5,0,1,0,5,'s',0,3,0,6,'p','X'},
        {2,60,0x81,0x6d,0,1,0,5,'s',0,3,0,6,'p',0xe0},
    };
    for (int i=0; i<3; ++i) {
        ethernet(true, false); const uint8_t snap[] = {0xaa,0xaa,3,0,0,12,0x20,0}; bytes(snap,8);
        bytes(vectors[i], i ? 15 : 14);
        size_t n=length-header; packet[12]=n>>8; packet[13]=n;
        valid(); CHECK(!strcmp(record.name,"s"));
    }
    CHECK(record.sanitized && !strcmp(record.port,"p?"));

    char long_text[256]; memset(long_text, 'A', 255); long_text[255] = 0;
    lldp_base(60, false); lldp_tlv(5, long_text, 255); lldp_tlv(4, long_text, 255); lldp_end(); valid();
    CHECK(strlen(record.name) == 255 && !strcmp(record.name, long_text) && strlen(record.description) == 255);
    lldp_base(60, false); lldp_tlv(5, long_text, 256); lldp_end(); CHECK(parse() == PARSE_LIMIT);
    lldp_base(60, false); lldp_tlv(5, "<script>evil()</script>", 23); lldp_end(); valid();
    CHECK(!strncmp(record.name,"<script>",8)); // Escaping/rendering belong to JSON/DOM, not identity parsing.
    // Both identity keys can use all 255 bytes; changes at the very end must not alias.
    ethernet(false,false);
    uint8_t identity[256]; memset(identity,'A',sizeof(identity)); identity[0]=7;
    lldp_tlv(1,identity,sizeof(identity)); size_t chassis_tail=length-1;
    lldp_tlv(2,identity,sizeof(identity)); const uint8_t life[]={0,120}; lldp_tlv(3,life,2); lldp_end();
    valid(); CHECK(record.chassis_length==255 && record.port_length==255 && strlen(record.port)==255);
    memset(&table,0,sizeof(table)); neighbors_link(&table,true,0);
    CHECK(neighbors_accept(&table,&record,table.session,100,100));
    packet[chassis_tail]='Z'; valid(); CHECK(neighbors_accept(&table,&record,table.session,100,100));
    CHECK(table.records[0].used && table.records[1].used && table.records[0].chassis_key[254]=='A' && table.records[1].chassis_key[254]=='Z');
    // Deterministic malformed/truncation mutation sweep. Output guards catch out-of-record writes.
    struct { uint32_t before; neighbor_t r; uint32_t after; } guarded;
    unsigned seed=12345;
    for (int i=0; i<10000; ++i) {
        lldp_base(120, i&1); lldp_optional(); lldp_end();
        seed=seed*1664525U+1013904223U; packet[seed % length] ^= (uint8_t)(seed >> 24);
        guarded.before=0x12345678; guarded.after=0xabcdef01;
        neighbor_parse(packet, seed % (length+1), &guarded.r);
        CHECK(guarded.before==0x12345678 && guarded.after==0xabcdef01);
    }
}

static void lifecycle_tests(void)
{
    memset(&table,0,sizeof(table)); CHECK(neighbors_state(&table,0)==DISC_LINK_DOWN);
    neighbors_link(&table,true,0); uint32_t session=table.session;
    CHECK(neighbors_state(&table,29999)==DISC_WAITING && neighbors_state(&table,30000)==DISC_NONE);
    lldp_base(2,false); lldp_optional(); lldp_end(); valid();
    CHECK(neighbors_accept(&table,&record,session,60000,60000)); // Late advertisements still accepted.
    CHECK(neighbors_state(&table,61999)==DISC_CURRENT && neighbors_state(&table,62000)==DISC_EXPIRED);
    CHECK(neighbor_expired(&table.records[0],62000));
    lldp_base(120,false); lldp_end(); valid(); CHECK(neighbors_accept(&table,&record,session,63000,63000));
    CHECK(!(table.records[0].fields & FIELD_NAME)); // Missing optional field never inherits stale data.
    lldp_base(0,false); lldp_end(); valid(); CHECK(neighbors_accept(&table,&record,session,64000,64000));
    CHECK(!table.records[0].used && neighbors_state(&table,64000)==DISC_EXPIRED);
    cdp_base(20,false,false); valid(); CHECK(neighbors_accept(&table,&record,session,65000,65000));
    cdp_base(0,false,false); valid(); CHECK(neighbors_accept(&table,&record,session,66000,66000));
    CHECK(!table.records[0].used);
    for (int i=0; i<NEIGHBOR_CAPACITY; ++i) {
        lldp_base(120,false); lldp_end(); packet[11]=i+1; valid();
        CHECK(neighbors_accept(&table,&record,session,67000,67000));
    }
    packet[11]=99; valid(); CHECK(!neighbors_accept(&table,&record,session,67000,67000));
    CHECK(table.capacity_drops==1 && table.records[0].source[5]==1);
    CHECK(neighbors_accept(&table,&record,session,200000,200000)); // Reuse first expired slot deterministically.
    CHECK(table.records[0].source[5]==99);
    neighbors_link(&table,false,201000);
    for (int i=0; i<NEIGHBOR_CAPACITY; ++i) CHECK(!table.records[i].used);
    CHECK(!neighbors_accept(&table,&record,session,200000,201000));
    neighbors_link(&table,true,202000);
    CHECK(!neighbors_accept(&table,&record,session,200000,202000)); // Old queued session cannot repopulate.
    CHECK(neighbors_state(&table,202000)==DISC_WAITING);
    CHECK(!neighbors_accept(&table,&record,table.session,1000,202000)); // Already stale in queue.
    CHECK(neighbors_accept(&table,&record,table.session,202000,202000));
    cdp_base(120,false,false); valid(); CHECK(neighbors_accept(&table,&record,table.session,202000,202000));
    CHECK(table.records[0].protocol != table.records[1].protocol); // Never merge protocols.
}

static void button_tests(void)
{
    button_logic_t s;
    button_logic_reset(&s,0);
    CHECK(!button_logic_update(&s,0,0)); CHECK(!button_logic_update(&s,0,30));
    CHECK(!button_logic_update(&s,BUTTON_OK,40)); CHECK(!button_logic_update(&s,BUTTON_OK,70));
    CHECK(!button_logic_update(&s,BUTTON_OK,569));
    CHECK(button_logic_update(&s,BUTTON_OK,570)==BUTTON_BACK);
    CHECK(!button_logic_update(&s,BUTTON_OK,2000));
    CHECK(!button_logic_update(&s,0,2010)); CHECK(!button_logic_update(&s,0,2040)); // Suppress short.
    CHECK(!button_logic_update(&s,BUTTON_OK,2100)); CHECK(!button_logic_update(&s,BUTTON_OK,2130));
    CHECK(!button_logic_update(&s,0,2200)); CHECK(button_logic_update(&s,0,2230)==BUTTON_OK);
    CHECK(!button_logic_update(&s,0,2300));
    CHECK(!button_logic_update(&s,BUTTON_UP,2310)); CHECK(button_logic_update(&s,BUTTON_UP,2340)==BUTTON_UP);
    CHECK(!button_logic_update(&s,BUTTON_UP,4000));
    // Editor-only repeat, saturation tested by the actual UI suite.
    s.repeat_enabled=true;
    CHECK(button_logic_update(&s,BUTTON_UP,4001)==BUTTON_UP_REPEAT);
    CHECK(!button_logic_update(&s,BUTTON_UP,4050));
    CHECK(!button_logic_update(&s,BUTTON_UP,4051));
    CHECK(button_logic_update(&s,BUTTON_UP,4140)==BUTTON_UP_REPEAT);
    CHECK(!button_logic_update(&s,BUTTON_UP,4189));
    CHECK(button_logic_update(&s,BUTTON_UP,4190)==BUTTON_UP_REPEAT);
    CHECK(!button_logic_update(&s,0,4200)); // Raw release suppresses a repeat during debounce.
    button_logic_reset(&s,0);button_logic_update(&s,0,0);button_logic_update(&s,0,30);
    s.repeat_enabled=true;button_logic_update(&s,BUTTON_DOWN,40);
    CHECK(button_logic_update(&s,BUTTON_DOWN,70)==BUTTON_DOWN);
    CHECK(!button_logic_update(&s,BUTTON_DOWN,519));
    CHECK(button_logic_update(&s,BUTTON_DOWN,520)==BUTTON_DOWN_REPEAT);
    CHECK(!button_logic_update(&s,BUTTON_DOWN,699));
    CHECK(button_logic_update(&s,BUTTON_DOWN,700)==BUTTON_DOWN_REPEAT);
    button_logic_reset(&s,BUTTON_OK); // Held through splash cannot navigate.
    CHECK(!button_logic_update(&s,BUTTON_OK,0)); CHECK(!button_logic_update(&s,BUTTON_OK,1000));
    CHECK(!button_logic_update(&s,0,1010)); CHECK(!button_logic_update(&s,0,1040));
    CHECK(!button_logic_update(&s,BUTTON_OK,1050)); CHECK(!button_logic_update(&s,BUTTON_OK,1080));
    CHECK(!button_logic_update(&s,0,1100)); CHECK(!button_logic_update(&s,BUTTON_OK,1110)); // Release bounce.
    CHECK(!button_logic_update(&s,0,1120)); CHECK(button_logic_update(&s,0,1150)==BUTTON_OK);
}

int main(void)
{
    parser_tests(); lifecycle_tests(); button_tests();
    printf("PASS: LLDP/CDP framing/fields/checksums, bounds, long text, expiry/withdrawal, capacity, sessions, OK debounce/hold\n");
    return 0;
}
