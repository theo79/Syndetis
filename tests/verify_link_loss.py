"""Evaluate actual production rules and actual DMA flush body without hardware.
Also verifies patch routing and UI/event wait invariants against source.
"""
import json
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[1]
compiler=json.loads((ROOT/'build/project_description.json').read_text())['c_compiler']
def compile_test(path):
    subprocess.run([compiler,'-x','c++','-std=gnu++20','-Wall','-Werror',
                    '-I',str(ROOT/'main'),'-I',str(ROOT/'patches'),'-fsyntax-only',str(path)],check=True)
compile_test(ROOT/'tests/link_loss_test.cpp')

# Test the actual patched flush function with a synthetic circular descriptor
# ring. Only constexpr qualifiers and hardware/cache/log no-ops are substituted.
driver=(ROOT/'patches/esp_eth_mac_esp_dma.c').read_text()
flush=driver[driver.index('void emac_esp_dma_flush_recv_frame('):driver.index('\nesp_err_t emac_esp_del_dma')]
flush=flush.replace('void emac_esp_dma_flush_recv_frame','constexpr void emac_esp_dma_flush_recv_frame',1)
fixture='''
#include "rx_ring_guard.h"
#define CONFIG_ETH_DMA_RX_BUFFER_NUM 4
#define EMAC_LL_DMADESC_OWNER_DMA 1
#define EMAC_HAL_DMA_DESC_SIZE 32
#define DMA_CACHE_INVALIDATE(a,b)
#define DMA_CACHE_WB(a,b)
#define ESP_LOGW(...)
struct eth_dma_rx_descriptor_t {
    struct { bool LastDescriptor; unsigned Own; } RDES0;
    eth_dma_rx_descriptor_t *Buffer2NextDescAddr;
};
struct emac_esp_dma_t { eth_dma_rx_descriptor_t *rx_desc; unsigned hal; };
using emac_esp_dma_handle_t=emac_esp_dma_t*;
constexpr void emac_hal_receive_poll_demand(unsigned *polls) { ++*polls; }
'''+flush+'''
constexpr bool test_flush(bool missing_last) {
    eth_dma_rx_descriptor_t ring[4]={};
    for(unsigned i=0;i<4;++i) ring[i].Buffer2NextDescAddr=&ring[(i+1)%4];
    if(!missing_last) ring[1].RDES0.LastDescriptor=true;
    emac_esp_dma_t dma={.rx_desc=&ring[0],.hal=0};
    emac_esp_dma_flush_recv_frame(&dma);
    if(dma.hal!=1 || ring[0].RDES0.Own!=1 || ring[1].RDES0.Own!=1) return false;
    if(missing_last) return dma.rx_desc==&ring[0] && ring[2].RDES0.Own==1 && ring[3].RDES0.Own==1;
    return dma.rx_desc==&ring[2] && ring[2].RDES0.Own==0 && ring[3].RDES0.Own==0;
}
static_assert(test_flush(true),"Missing last descriptor must return after one ring");
static_assert(test_flush(false),"Valid frame must return only its descriptors");
'''
fixture_path=ROOT/'build/link_loss_flush_test.cpp'
fixture_path.write_text(fixture)
compile_test(fixture_path)

# Confirm the same missing-marker ring defeats the unmodified installed routine.
sdk_driver=Path('C:/esp/v6.1/esp-idf/components/esp_eth/src/mac/esp_eth_mac_esp_dma.c').read_text()
original=sdk_driver[sdk_driver.index('void emac_esp_dma_flush_recv_frame('):sdk_driver.index('\nesp_err_t emac_esp_del_dma')]
original=original.replace('void emac_esp_dma_flush_recv_frame','constexpr void emac_esp_dma_flush_recv_frame',1)
broken=fixture.replace(flush,original)
original_path=ROOT/'build/link_loss_original_flush_test.cpp'
original_path.write_text(broken)
repro=subprocess.run([compiler,'-x','c++','-std=gnu++20','-fconstexpr-ops-limit=10000',
                      '-I',str(ROOT/'patches'),'-fsyntax-only',str(original_path)],capture_output=True,text=True)
assert repro.returncode!=0 and 'constexpr' in repro.stderr and ('exceeds' in repro.stderr or 'exceeded' in repro.stderr),repro.stderr

discovery=(ROOT/'main/discovery.c').read_text()
network=(ROOT/'main/network_status.c').read_text()
buttons=(ROOT/'main/ui_buttons.c').read_text()
assert 'xSemaphoreCreateMutex' not in discovery
assert discovery.count('portMAX_DELAY')==1 and 'ulTaskNotifyTake(pdTRUE, portMAX_DELAY)' in discovery
assert 'portMAX_DELAY' not in network and 'portMAX_DELAY' not in buttons
assert 'xQueueSend(button_queue, &button, 0)' in (ROOT/'main/ui_menu.c').read_text()
assert 'requested_revision=n.revision' in discovery
scan=discovery[discovery.index('static void scan(void)'):discovery.index('static void task(')]
for match in re.finditer(r'return;',scan):
    assert re.search(r'finish\([^;]+\);\s*$',scan[:match.start()]),'Scan return bypasses common cleanup'
assert 'diagnostic_release(run_token)' in discovery
autotest=(ROOT/'main/autotest.c').read_text()
worker=autotest[autotest.index('static void autotest_task'):autotest.index('esp_err_t autotest_start')]
assert 'execute(&result)' in worker and 'diagnostic_release(run_token)' in worker and 'return' not in worker
for source in (discovery,autotest):
    assert 'diagnostic_current(' in source and 'diagnostic_accept(' in source
assert 'requested_generation=n.link_generation' in autotest
for name in ('on_eth','on_ip'):
    callback=network[network.index('static void '+name):]
    callback=callback[:callback.index('\n}\n')]
    assert not re.search(r'xSemaphoreTake|vTaskDelay|esp_ping_|ui_oled_|esp_netif_',callback)
main=(ROOT/'main/app_main.c').read_text()
assert main.index('network_status_init(eth_handle)')<main.index('esp_netif_attach(eth_netif, eth_glue)')
assert '#define PROBES 4' in discovery and '#define TIMEOUT_MS 250' in discovery and 'pause_checked(n.revision,1100)' in discovery
assert 'config.count = 3;' in autotest and 'config.timeout_ms = 1000;' in autotest and 'config.interval_ms = 500;' in autotest
commands=json.loads((ROOT/'build/compile_commands.json').read_text())
for name in ('esp_eth_mac_esp_dma.c','ping_sock.c'):
    matches=[item['file'].replace('\\','/') for item in commands if item['file'].replace('\\','/').endswith('/'+name)]
    assert len(matches)==1 and '/patches/' in matches[0],(name,matches)
ping=(ROOT/'patches/ping_sock.c').read_text()
assert 'ping_receive_continue(' in ping and 'atomic_uint flags' in ping
assert 'while (!desc_iter->RDES0.LastDescriptor)' in driver and 'rx_ring_step(&walked, CONFIG_ETH_DMA_RX_BUFFER_NUM)' in driver
print('PASS: actual cancellation/reservation/generation rules; idle/down/up/DHCP; both diagnostics abort/restart; stale callback/release; lost IP; common error cleanup; bounded ping/ring guards; actual DMA flush with missing/valid terminal descriptor; source wait invariants and active build patch routing.')
