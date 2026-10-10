"""GCC constexpr evaluation of actual OLED formatter/navigation, no hardware.

Only constexpr qualifiers and SDK-independent declarations are supplied. The
formatter uses no libc adapters and the menu navigation branches are extracted
from production source. This is a lightweight extension of existing fixtures.
"""
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/switch_info_ui_test.cpp'
compiler = json.loads((ROOT / 'build/project_description.json').read_text())['c_compiler']
source = (ROOT / 'main/ui_switch_info.c').read_text()
source = re.sub(r'^#include .*\n', '', source, flags=re.M)
source = re.sub(r'^(static )?(size_t|void|unsigned|const char) (\*?\w+)\(', r'\1constexpr \2 \3(', source, flags=re.M)
menu = (ROOT / 'main/ui_menu.c').read_text()
navigation = menu[menu.index('    if (screen == MAIN || screen == SYSTEM)'):menu.index('    } else if (screen == DISC_READY)')]
navigation = navigation[:navigation.index('    } else if (screen == INDUSTRIAL')] + navigation[navigation.index('    } else if (screen == SWITCH_INFO'):]
navigation += '    }\n'
fixture = r'''
#include "switch_info_parse.h"
#define UI_SWITCH_INFO_PAGES 5
'''+source+r'''
constexpr bool equal(const char *a, const char *b) {
    for (unsigned i=0; i<22; ++i) { if (a[i]!=b[i]) return false; if (!a[i]) return true; }
    return false;
}
constexpr bool safe_rows(char rows[8][22]) {
    for (unsigned r=0; r<8; ++r) {
        bool ended=false;
        for (unsigned c=0; c<22; ++c) {
            if (!rows[r][c]) { ended=true; break; }
            if ((unsigned char)rows[r][c]<32 || (unsigned char)rows[r][c]>126) return false;
        }
        if (!ended) return false;
    }
    return true;
}
constexpr int render_suite() {
    switch_neighbor_t n={}; char rows[8][22]={};
    ui_switch_info_render(rows,4,false,0x105,&n);
    if (!equal(rows[2],"Unavailable") || !equal(rows[5],"Code: 0x105")) return 1;
    ui_switch_info_render(rows,3,true,0,&n);
    if (!equal(rows[2],"No neighbor found") || !equal(rows[6],"Listener active") ||
        !equal(rows[4],"Waiting for LLDP/CDP")) return 2;
    n.valid=true; n.protocol=SWITCH_PROTOCOL_LLDP; n.ttl=120;
    const char *name="SW-CORE-01"; const char *port="Gi1/0/24";
    for (unsigned i=0; name[i]; ++i) n.system_name[i]=name[i];
    for (unsigned i=0; port[i]; ++i) n.port_id[i]=port[i];
    ui_switch_info_render(rows,0,true,0,&n);
    if (!equal(rows[0],"SWITCH INFO 1/5") || !equal(rows[1],"Proto: LLDP") ||
        !equal(rows[3],name) || !equal(rows[5],port) || !equal(rows[6],"TTL: 120s")) return 3;
    for (unsigned page=0; page<5; ++page) {
        ui_switch_info_render(rows,page,true,0,&n);
        if (!safe_rows(rows)) return 4;
    }
    ui_switch_info_render(rows,2,true,0,&n);
    if (!equal(rows[3],"N/A") || !equal(rows[5],"PVID: N/A")) return 5;
    n.source_mac[0]=0; n.source_mac[1]=0x11; n.source_mac[2]=0x22;
    n.source_mac[3]=0x33; n.source_mac[4]=0x44; n.source_mac[5]=0x55;
    ui_switch_info_render(rows,1,true,0,&n);
    if (!equal(rows[6],"MAC:00:11:22:33:44:55")) return 17;
    ui_switch_info_render(rows,3,true,0,&n);
    if (!equal(rows[2],"N/A") || !equal(rows[5],"N/A")) return 6;
    ui_switch_info_render(rows,4,true,0,&n);
    if (!equal(rows[2],"Supported: N/A") || !equal(rows[4],"Enabled: N/A")) return 7;
    n.management_ipv4_present=true; n.management_ipv4[0]=192; n.management_ipv4[1]=168;
    n.management_ipv4[2]=1; n.management_ipv4[3]=2; n.pvid_present=true; n.pvid=100;
    ui_switch_info_render(rows,2,true,0,&n);
    if (!equal(rows[3],"192.168.1.2") || !equal(rows[5],"PVID: 100")) return 8;
    n.capabilities_present=true; n.supported_capabilities=0x14; n.enabled_capabilities=4;
    ui_switch_info_render(rows,4,true,0,&n);
    if (!equal(rows[2],"Supported: 0x0014") || !equal(rows[3],"Bridge Router") ||
        !equal(rows[4],"Enabled: 0x0004") || !equal(rows[5],"Bridge")) return 9;
    n.supported_capabilities=0xffff; n.enabled_capabilities=0;
    ui_switch_info_render(rows,4,true,0,&n);
    if (!safe_rows(rows) || !equal(rows[5],"None")) return 10;
    /* Deliberately fill entire arrays without NUL: all accesses must stay bounded. */
    for (auto &c:n.system_name) c='S';
    for (auto &c:n.port_id) c='P';
    for (auto &c:n.chassis_id) c='C';
    for (auto &c:n.system_description) c='D';
    for (auto &c:n.port_description) c='E';
    ui_switch_info_render(rows,0,true,0,&n);
    if (!equal(rows[3],"SSSSSSSSSSSSSSSSSS...") || !equal(rows[5],"PPPPPPPPPPPPPPPPPP...")) return 11;
    ui_switch_info_render(rows,3,true,0,&n);
    if (!equal(rows[2],"DDDDDDDDDDDDDDDDDDDDD") || !equal(rows[3],"DDDDDDDDDDDDDDDDDD...") ||
        !equal(rows[5],"EEEEEEEEEEEEEEEEEEEEE") || !equal(rows[6],"EEEEEEEEEEEEEEEEEE...")) return 12;
    n.system_name[0]='\n'; n.port_id[0]=(char)0xff;
    for (unsigned page=0; page<5; ++page) {
        ui_switch_info_render(rows,page,true,0,&n);
        if (!safe_rows(rows)) return 13;
    }
    n.protocol=SWITCH_PROTOCOL_CDP;
    ui_switch_info_render(rows,0,true,0,&n);
    if (!equal(rows[1],"Proto: CDP")) return 14;
    n.cdp_capabilities_present=true; n.cdp_capabilities=0x80000029;
    n.duplex_present=true; n.full_duplex=true; n.platform[0]='C'; n.platform[1]=0;
    ui_switch_info_render(rows,2,true,0,&n);
    if (!equal(rows[5],"Native VLAN: 100") || !equal(rows[6],"Peer duplex: FULL")) return 18;
    ui_switch_info_render(rows,3,true,0,&n);
    if (!equal(rows[4],"Platform:") || !equal(rows[5],"C")) return 19;
    ui_switch_info_render(rows,4,true,0,&n);
    if (!equal(rows[2],"CDP raw: 0x80000029") || !equal(rows[4],"Enabled: N/A")) return 20;
    n.protocol=SWITCH_PROTOCOL_NONE;
    ui_switch_info_render(rows,0,true,0,&n);
    if (!equal(rows[1],"Proto: NONE")) return 15;
    n.valid=false; /* Expiry/lifecycle is authoritative: ignore retained fixture fields. */
    ui_switch_info_render(rows,4,true,0,&n);
    if (!equal(rows[2],"No neighbor found") || rows[3][0] || rows[5][0]) return 16;
    return 0;
}
static_assert(render_suite()==0,"Actual Switch Info render fixture failed");
enum screen_t {MAIN,SYSTEM,PLACEHOLDER,STATUS,ABOUT,CONFIRM_UPDATE,TEST_READY,DISC_READY,SWITCH_INFO,INDUSTRIAL};
enum ui_button_t {UI_BUTTON_UP,UI_BUTTON_OK,UI_BUTTON_DOWN};
constexpr void navigate_fixture(ui_button_t button, screen_t &screen, unsigned &main_selection,
                                unsigned &system_selection, unsigned &switch_page,
                                bool &discovery_start_failed) {
'''+navigation+r'''
}
constexpr int navigation_suite() {
    screen_t screen=MAIN; unsigned main_selection=2, system_selection=0, switch_page=4;
    bool discovery_start_failed=false;
    navigate_fixture(UI_BUTTON_OK,screen,main_selection,system_selection,switch_page,discovery_start_failed);
    if (screen!=SWITCH_INFO || switch_page!=0) return 1;
    navigate_fixture(UI_BUTTON_UP,screen,main_selection,system_selection,switch_page,discovery_start_failed);
    if (switch_page!=4) return 2;
    navigate_fixture(UI_BUTTON_DOWN,screen,main_selection,system_selection,switch_page,discovery_start_failed);
    if (switch_page!=0) return 3;
    for (unsigned i=0;i<5;++i) {
        navigate_fixture(UI_BUTTON_DOWN,screen,main_selection,system_selection,switch_page,discovery_start_failed);
        if (switch_page!=(i+1)%5) return 4;
    }
    navigate_fixture(UI_BUTTON_OK,screen,main_selection,system_selection,switch_page,discovery_start_failed);
    if (screen!=MAIN || main_selection!=2) return 5;
    for (unsigned selected=0; selected<6; ++selected) {
        screen=MAIN; main_selection=selected;
        navigate_fixture(UI_BUTTON_OK,screen,main_selection,system_selection,switch_page,discovery_start_failed);
        const screen_t expected[]={TEST_READY,DISC_READY,SWITCH_INFO,PLACEHOLDER,INDUSTRIAL,SYSTEM};
        if (screen!=expected[selected]) return 6;
    }
    return 0;
}
static_assert(navigation_suite()==0,"Actual Switch Info menu navigation failed");
'''
OUT.write_text(fixture)
subprocess.run([compiler,'-x','c++','-std=gnu++23','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'main'),'-fsyntax-only',str(OUT)],check=True)
assert '"Switch Info"' in menu
assert 'switch_info_get(&switch_snapshot);' in menu
assert 'if (!switch_snapshot.available || !switch_snapshot.neighbor.valid) switch_page = 0;' in menu
assert menu.index('switch_info_get(&switch_snapshot);') < menu.index('ui_switch_info_render(rows,')
assert 'pdMS_TO_TICKS(200)' in menu and 'memcmp(rows, previous, sizeof(rows))' in menu
assert 'expiration_us' not in source and 'switch_info_parse_lldp' not in source
assert 'OTA_RECEIVING || ota->phase == OTA_SUCCESS' in menu
print('PASS: actual OLED formatter: unavailable/waiting/LLDP/CDP enum, all five pages, optional values, absent fields, bounded nonterminated strings/control bytes, expiry clears displayed data.')
print('PASS: actual menu entry/UP/DOWN wrap/OK exit branches; other menu routes preserved; copied getter, page reset, existing redraw cadence and OTA priority source invariants.')
