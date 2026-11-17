#include <assert.h>
#include "pocketos-radio-idle.h"
int main(void){
    struct po_radio_observation s={
        .wifi_link=PO_LINK_IDLE,.bt_clients=PO_LINK_IDLE,
        .wifi_enabled=1,.bt_enabled=1,
        .wifi_idle_since_ms=100,.bt_idle_since_ms=100
    };
    assert(po_radio_idle_advice(s,300099)==PO_RADIO_KEEP);
    assert(po_radio_idle_advice(s,300100)==(PO_RADIO_SLEEP_WIFI|PO_RADIO_SLEEP_BT));
    assert(po_radio_idle_advice(s,-1)==PO_RADIO_KEEP);
    s.wifi_link=PO_LINK_UNKNOWN;
    assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);
    s.wifi_link=PO_LINK_ACTIVE;s.bt_clients=PO_LINK_ACTIVE;
    assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);
    s.wifi_link=PO_LINK_IDLE;s.bt_clients=PO_LINK_IDLE;
    s.update_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.update_busy=0;
    s.transfer_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.transfer_busy=0;
    s.radio_job_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.radio_job_busy=0;
    s.external_radio_lease=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.external_radio_lease=0;
    s.discovery_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.discovery_busy=0;
    s.audio_bt_playing=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);s.audio_bt_playing=0;
    s.wifi_page=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);s.wifi_page=0;
    s.bt_page=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);s.bt_page=0;
    s.wifi_enabled=0;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);s.wifi_enabled=1;
    s.bt_enabled=0;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);s.bt_enabled=1;
    s.wifi_idle_since_ms=-1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);s.wifi_idle_since_ms=100;
    s.bt_clients=PO_LINK_UNKNOWN;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);
    return 0;
}
