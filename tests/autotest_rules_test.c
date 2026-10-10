/* Compile-time tests exercise the same pure policy expressions as firmware.
 * No target execution, network traffic, or hardware is required.
 */
#include "autotest_rules.h"
#define EXPECT(expression) _Static_assert((expression), #expression)
EXPECT(AT_LINK(false) == AT_FAIL);
EXPECT(AT_LINK(true) == AT_PASS);
EXPECT(AT_NEGOTIATION(true, true) == AT_PASS);
EXPECT(AT_NEGOTIATION(true, false) == AT_WARN);
EXPECT(AT_NEGOTIATION(false, true) == AT_WARN);
EXPECT(AT_DHCP(false, false, false) == AT_SKIP);
EXPECT(AT_DHCP(true, false, true) == AT_FAIL);
EXPECT(AT_DHCP(true, true, true) == AT_PASS);
EXPECT(AT_DHCP(true, true, false) == AT_WARN);
EXPECT(AT_GATEWAY(true, 3, 3) == AT_PASS);
EXPECT(AT_GATEWAY(true, 3, 2) == AT_WARN);
EXPECT(AT_GATEWAY(true, 3, 0) == AT_FAIL);
EXPECT(AT_GATEWAY(true, 0, 0) == AT_FAIL);
EXPECT(AT_GATEWAY(false, 0, 0) == AT_WARN);
EXPECT(AT_DNS(false) == AT_WARN);
EXPECT(AT_DNS(true) == AT_PASS);
EXPECT(AT_RESOLUTION(false) == AT_WARN);
EXPECT(AT_RESOLUTION(true) == AT_PASS);
EXPECT(AT_MERGE(AT_PASS, AT_SKIP) == AT_PASS);
EXPECT(AT_MERGE(AT_PASS, AT_WARN) == AT_WARN);
EXPECT(AT_MERGE(AT_WARN, AT_FAIL) == AT_FAIL);
EXPECT(AT_MERGE(AT_FAIL, AT_WARN) == AT_FAIL);
/* An isolated local network passes with configured DNS and no resolution target. */
EXPECT(AT_MERGE(AT_MERGE(AT_GATEWAY(true, 3, 3), AT_DNS(true)), AT_SKIP) == AT_PASS);
EXPECT(AT_IP_VALID(0xc0a8015eU, 0xffffff00U)); /* 192.168.1.94 /24 */
EXPECT(!AT_IP_VALID(0, 0xffffff00U));
EXPECT(!AT_IP_VALID(0x7f000001U, 0xff000000U)); /* loopback */
EXPECT(!AT_IP_VALID(0xe0000001U, 0xffffff00U)); /* multicast */
EXPECT(!AT_IP_VALID(0xc0a80100U, 0xffffff00U)); /* subnet address */
EXPECT(!AT_IP_VALID(0xc0a801ffU, 0xffffff00U)); /* broadcast */
EXPECT(!AT_IP_VALID(0xc0a8015eU, 0xff00ff00U)); /* non-contiguous */
EXPECT(!AT_IP_VALID(0xc0a8015eU, 0));
EXPECT(AT_IP_VALID(0xc0a80100U, 0xfffffffeU)); /* /31 point-to-point */
EXPECT(AT_IP_VALID(0xc0a8015eU, 0xffffffffU)); /* /32 */
EXPECT(AT_GATEWAY_VALID(0xc0a8015eU, 0xffffff00U, 0xc0a80101U));
EXPECT(AT_GATEWAY_VALID(0xc0a8015eU, 0xffffff00U, 0));
EXPECT(!AT_GATEWAY_VALID(0xc0a8015eU, 0xffffff00U, 0xc0a80201U));
EXPECT(!AT_GATEWAY_VALID(0xc0a8015eU, 0xffffff00U, 0xc0a8015eU));
EXPECT(!AT_GATEWAY_VALID(0xc0a8015eU, 0xffffff00U, 0xc0a801ffU));
EXPECT(AT_GATEWAY_VALID(0xc0a80100U, 0xfffffffeU, 0xc0a80101U));
