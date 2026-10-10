#include "diagnostic_rules.h"
#include "ping_receive_guard.h"
#include "rx_ring_guard.h"
#define EXPECT(x) do { if (!(x)) return __LINE__; } while (0)
constexpr int link_loss_tests()
{
    network_lifecycle_t net={};
    diagnostic_reservation_t owner={};
    network_link_event(&net,true);
    EXPECT(net.link_up && !net.ipv4_ready);
    uint32_t previous=net.link_generation;
    network_link_event(&net,false); // Idle UP -> DOWN.
    EXPECT(!net.link_up && !net.ipv4_ready && net.link_generation!=previous && !owner.active);
    EXPECT(!network_ip_event(&net,true,true)); // Late DHCP while disconnected.
    EXPECT(!net.ipv4_ready);
    network_link_event(&net,true); // DOWN -> UP -> DHCP.
    EXPECT(net.link_up && !net.ipv4_ready);
    EXPECT(network_ip_event(&net,true,true));
    EXPECT(net.ipv4_ready);
    // Same production rules for Discover ARP, Discover ICMP, and AutoTest.
    for (unsigned operation=0;operation<3;++operation) {
        uint32_t token=diagnostic_reserve(&owner);
        EXPECT(token && !diagnostic_reserve(&owner));
        uint32_t generation=net.revision;
        EXPECT(diagnostic_accept(token,owner.active,generation,net.revision,net.link_up));
        network_link_event(&net,false);
        EXPECT(!diagnostic_accept(token,owner.active,generation,net.revision,net.link_up));
        EXPECT(diagnostic_unreserve(&owner,token));
        EXPECT(!owner.active && !diagnostic_unreserve(&owner,token));
        network_link_event(&net,true);
        EXPECT(network_ip_event(&net,true,true));
        uint32_t next=diagnostic_reserve(&owner);
        EXPECT(next && next!=token);
        EXPECT(!diagnostic_accept(token,owner.active,generation,net.revision,net.link_up));
        EXPECT(!diagnostic_unreserve(&owner,token)); // Stale release cannot free new owner.
        EXPECT(owner.active==next);
        EXPECT(diagnostic_accept(next,owner.active,net.revision,net.revision,net.link_up));
        EXPECT(diagnostic_unreserve(&owner,next));
    }
    uint32_t token=diagnostic_reserve(&owner),generation=net.revision;
    EXPECT(network_ip_event(&net,false,true)); // Lost IP without physical DOWN.
    EXPECT(!diagnostic_accept(token,owner.active,generation,net.revision,net.link_up));
    EXPECT(diagnostic_unreserve(&owner,token));
    // All error/abort classes free reservations through the common cleanup.
    for (unsigned failure=0;failure<12;++failure) {
        token=diagnostic_reserve(&owner);
        EXPECT(token && diagnostic_unreserve(&owner,token) && !owner.active);
    }
    // Tickets remain nonzero across wrap and stale callbacks stay rejected.
    owner.sequence=UINT32_MAX;
    token=diagnostic_reserve(&owner);
    EXPECT(token==1 && diagnostic_unreserve(&owner,token));
    EXPECT(ping_receive_continue(true,249,250));
    EXPECT(!ping_receive_continue(true,250,250)); // Endless unrelated packets have an absolute budget.
    EXPECT(!ping_receive_continue(false,0,1000)); // Stop beats receive deadline.
    EXPECT(!ping_receive_continue(true,(uint32_t)(15U-0xfffffff0U),30)); // Uptime wrap.
    unsigned walked=0;
    while(rx_ring_step(&walked,10)) {}
    EXPECT(walked==10 && !rx_ring_step(&walked,10));
    return 0;
}
static_assert(link_loss_tests()==0,"Link-loss/cancellation assertion failed");
