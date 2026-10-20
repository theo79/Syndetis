// Synthetic test-only API data; never compiled into firmware.
exports.status = (down = false, expired = false) => ({
  firmware: '0.3.0', uptime_seconds: 3661,
  ethernet: {initialization: 'Ready', error_code: 0, error_step: '',
    link: down ? 'Down' : 'Up', speed_mbps: down ? null : 100,
    duplex: down ? 'Unavailable' : 'Full', mac: '02:00:00:00:00:01', ip: 'Not configured'},
  switch_info: {state: down ? 'Link down' : expired ? 'Expired' : 'Received/current',
    observation_seconds: 30, capacity: 4, capacity_drops: 0, queue_drops: 0, limited_frames: 0, invalid_frames: 0,
    neighbors: down ? [] : [
      {slot: 1, protocol: 'LLDP', source_mac: '02:00:00:00:00:02',
        name: '<img src=x onerror="window.injected=true">'+'long-name-'.repeat(20),
        chassis: '02:00:00:00:00:02', port_id: 'Gi1/0/1', port_description: '',
        management_ip: '192.0.2.9', management_source: 'Management address TLV',
        vlan: 100, vlan_kind: 'Port PVID', ttl_seconds: 120, age_seconds: expired ? 121 : 5, expired, sanitized: false},
      {slot: 2, protocol: 'CDP', source_mac: '02:00:00:00:00:03', name: 'switch-cdp', chassis: null,
        port_id: 'Gi1/0/7', port_description: null, management_ip: '192.0.2.10',
        management_source: 'CDP address TLV', vlan: 200, vlan_kind: 'Native VLAN',
        ttl_seconds: 120, age_seconds: expired ? 121 : 5, expired, sanitized: false},
    ]},
});
